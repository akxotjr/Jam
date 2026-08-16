#include "pch.h"
#include "jamnet/runtime/world/lifecycle/ServerWorldTransitionCoordinator.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/utils/Clock.h"
#include "jamnet/runtime/application/ServerNetworkManager.h"
#include "jamnet/runtime/session/RuntimeShardRouting.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/protocol/codec/WorldCodec.h"
#include "jamnet/runtime/world/lifecycle/WorldShardState.h"
#include "jamnet/runtime/world/lifecycle/WorldActionTypes.h"
#include "jamnet/runtime/world/data/WorldArchetypesLoader.h"
#include "jamnet/runtime/world/data/WorldInstancesLoader.h"
#include "jamnet/runtime/world/data/WorldTemplatesLoader.h"
#include "jamnet/runtime/world/actor/ActorArchetypesLoader.h"
#include "jamnet/runtime/world/data/ActorLevelsLoader.h"
#include "jamnet/runtime/world/simulation/server/ServerWorld.h"

namespace jam::net
{
	namespace
	{
		std::optional<WorldRecord> CreateWorldOnShard(
			WorldShardState& state,
			const RouteAssignment& route,
			const WorldConfig& config,
			const ActorArchetypeDatabase& actorArchetypes,
			ServerNetworkManager& owner,
			std::weak_ptr<ServerWorldTransitionCoordinator> coordinator)
		{
			if (state.shardIndex != route.shardIndex)
				return std::nullopt;

			const WorldId worldId = state.AllocWorldId();
			if (worldId == kInvalidWorldId)
				return std::nullopt;

			WorldConfig resolved = config;
			resolved.world.worldId = worldId;

			auto world = std::make_unique<ServerWorld>(
				resolved,
				std::move(owner.CreateWorldContent(resolved)),
				[coordinator](UserId userId, const EnterWorldRequest& request)
				{
					if (const auto target = coordinator.lock())
						target->Enter(userId, request, NOW_NS());
				});

			world->SetActorArchetypeDatabase(actorArchetypes);
			if (!resolved.actorLevelPath.empty())
				world->SetActorLevelDatabase(ActorLevelsLoader::Load(resolved.actorLevelPath));

			state.RegisterWorld(resolved);
			if (!world->Init())
			{
				state.FreeWorldId(worldId);
				return std::nullopt;
			}

			if (!state.AdoptWorld(std::move(world)))
			{
				state.FreeWorldId(worldId);
				return std::nullopt;
			}

			auto* activeWorld = dynamic_cast<ServerWorld*>(state.FindWorld(worldId));
			if (!activeWorld)
			{
				state.FreeWorldId(worldId);
				return std::nullopt;
			}
			activeWorld->Start(1'000'000'000ull / 30ull);

			const WorldRecord* record = state.FindAuthoritativeWorldEntry(worldId);
			return record ? std::optional(*record) : std::nullopt;
		}
	}




	bool ServerWorldTransitionCoordinator::Initialize(ServerNetworkManager* owner)
	{
		m_owner = owner;
		if (!owner || !owner->GetSharedDataManifest())
			return false;
		m_reconnectTimeoutNs = owner->GetReconnectTimeoutNs();

		const auto* manifest = owner->GetSharedDataManifest();
		m_templatesDB       = WorldTemplatesLoader::Load(manifest->worldTemplateDatabasePath);
		m_archetypesDB      = WorldArchetypesLoader::Load(manifest->worldArchetypeDatabasePath);
		m_instancesDB       = WorldInstancesLoader::Load(manifest->worldInstanceDatabasePath);
		m_actorArchetypesDB = ActorArchetypesLoader::Load(manifest->actorArchetypeDatabasePath);
		m_configResolver    = std::make_unique<WorldConfigResolver>(manifest, &m_templatesDB, &m_archetypesDB);

		for (const auto& definition : m_instancesDB.definitionsByName | std::views::values)
		{
			if (!m_archetypesDB.Find(definition.archetypeKey))
				throw std::runtime_error("world instance references an unknown archetype: " + definition.name);
		}
		return true;
	}

	void ServerWorldTransitionCoordinator::Shutdown()
	{
		struct ShutdownBarrier
		{
			std::mutex mutex;
			std::condition_variable cv;
			size_t remainingShards = 0;
		};

		auto barrier = std::make_shared<ShutdownBarrier>();
		for (const auto& shard : GLOBAL_EXEC.GetShards())
			if (shard)
				++barrier->remainingShards;

		auto shardDone = [barrier]()
			{
				std::scoped_lock lock(barrier->mutex);
				if (barrier->remainingShards > 0 && --barrier->remainingShards == 0)
					barrier->cv.notify_all();
			};

		for (const auto& shard : GLOBAL_EXEC.GetShards())
		{
			if (!shard)
				continue;

			shard->Submit(Job([shardDone]()
				{
					auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
					std::vector<WorldId> worldIds;
					for (const auto& entry : state.worldsById.entries)
						if (entry.object && entry.state == eShardOwnedObjectState::Alive)
							worldIds.push_back(entry.object->GetWorldId());

					if (worldIds.empty())
					{
						shardDone();
						return;
					}

					auto remaining = std::make_shared<std::atomic<size_t>>(worldIds.size());
					for (const WorldId worldId : worldIds)
					{
						if (!state.BeginDestroyWorld(worldId, eMailboxCloseMode::Abort,
							[remaining, shardDone]()
								{
									if (remaining->fetch_sub(1, std::memory_order_acq_rel) == 1)
										shardDone();
								}))
						{
							if (remaining->fetch_sub(1, std::memory_order_acq_rel) == 1)
								shardDone();
						}
					}
				}, eJobPriority::Control));
		}

		{
			std::unique_lock lock(barrier->mutex);
			barrier->cv.wait(lock, [&barrier]() { return barrier->remainingShards == 0; });
		}

		std::vector<WorldReadyFn> worldWaiters;
		{
			std::scoped_lock lock(m_worldMutex);
			for (auto& slot : m_worldSlots | std::views::values)
				for (auto& waiter : slot.waiters)
					worldWaiters.push_back(std::move(waiter));
			m_worldSlots.clear();
		}
		for (auto& waiter : worldWaiters)
		{
			if (!waiter)
				continue;
			waiter(std::nullopt);
		}

		m_configResolver.reset();
		m_owner = nullptr;
	}

	void ServerWorldTransitionCoordinator::BootstrapConfiguredWorlds(std::function<void(bool)> completed)
	{
		std::vector<WorldInstanceRef> instances;
		for (const auto& definition : m_instancesDB.definitionsByName | std::views::values)
		{
			if (definition.startup == eWorldInstanceStartup::Bootstrap)
				instances.push_back(definition.Ref());
		}

		if (instances.empty())
		{
			if (completed) completed(true);
			return;
		}

		struct BootstrapState
		{
			std::atomic<size_t>			remaining = 0;
			std::atomic<bool>			succeeded = true;
			std::function<void(bool)>	completed;
		};

		auto state = std::make_shared<BootstrapState>();
		state->remaining.store(instances.size(), std::memory_order_relaxed);
		state->completed = std::move(completed);
		
		for (const auto& instance : instances)
		{
			EnsureWorldAsync(instance, [state](std::optional<WorldRef> world)
				{
					if (!world)
						state->succeeded.store(false, std::memory_order_relaxed);
					if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1 && state->completed)
						state->completed(state->succeeded.load(std::memory_order_relaxed));
				});
		}
	}

	void ServerWorldTransitionCoordinator::DestroyWorld(const WorldRef& world)
	{
		if (!world.IsValid())
			return;
		{
			std::scoped_lock lock(m_worldMutex);
			auto it = m_worldSlots.find(world.instance.instanceId.value);
			if (it != m_worldSlots.end() && it->second.world == world)
			{
				if (it->second.state == eWorldSlotState::Destroying)
					return;
				if (it->second.state == eWorldSlotState::Ready)
					it->second.state = eWorldSlotState::Destroying;
			}
		}

		const auto weak = weak_from_this();
		if (!SubmitToWorldShard(world.worldId, Job([world, weak]()
			{
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				if (const auto* entry = state.FindAuthoritativeWorldEntry(world.worldId))
				{
					if (!state.BeginDestroyWorld(world.worldId, eMailboxCloseMode::Abort, [world, weak]()
						{
							if (const auto coordinator = weak.lock())
								coordinator->CompleteWorldDestruction(world, true);
						}))
					{
						if (const auto coordinator = weak.lock())
							coordinator->CompleteWorldDestruction(world, false);
					}
				}
				else if (const auto coordinator = weak.lock())
				{
					coordinator->CompleteWorldDestruction(world, true);
				}
			}, eJobPriority::Control)))
		{
			CompleteWorldDestruction(world, false);
			JAM_LOG_WARN("Failed to route world destroy request to world shard. worldId={}", world.worldId);
		}
	}

	void ServerWorldTransitionCoordinator::Enter(UserId userId, const EnterWorldRequest& request, uint64 nowNs)
	{
		if (!SubmitToUserShard(userId, Job(weak_from_this(), &ServerWorldTransitionCoordinator::EnterOnUserShard, eJobPriority::Control, userId, request, nowNs)))
		{
			JAM_LOG_WARN("Failed to route world enter request to user shard. userId={}", userId);
		}
	}

	void ServerWorldTransitionCoordinator::Leave(UserId userId, const LeaveWorldRequest& request)
	{
		if (!SubmitToUserShard(userId, Job(weak_from_this(), &ServerWorldTransitionCoordinator::LeaveOnUserShard, eJobPriority::Control, userId, request)))
		{
			JAM_LOG_WARN("Failed to route world leave request to user shard. userId={}", userId);
		}
	}



	UserContext* ServerWorldTransitionCoordinator::FindUserOnCurrentShard(uint64 userId) const
	{
		auto* local = CurrentShardLocal();
		if (!local || local->shardIndex != GetUserShardIndex(userId))
			return nullptr;
		return GetOrCreateUserShardState(*local).FindUserContext(userId);
	}

	UserContext* ServerWorldTransitionCoordinator::FindExpectedTransition(const WorldTransitionContinuation& continuation) const
	{
		UserContext* user = FindUserOnCurrentShard(continuation.userId);
		if (!user || !user->worldTransition.active)
			return nullptr;

		const WorldTransitionState& transition = *user->worldTransition.active;
		return transition.token == continuation.token && transition.phase == continuation.expectedPhase ? user : nullptr;
	}

	std::optional<UserWorldState> ServerWorldTransitionCoordinator::CommitMain(uint64 userId, WorldStateRevision expectedRevision, std::optional<WorldRef> main)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user || user->worldState.revision != expectedRevision)
			return std::nullopt;

		if (main)
		{
			if (!user->worldState.SetMain(*main) && (!user->worldState.main || *user->worldState.main != *main))
				return std::nullopt;
		}
		else if (user->worldState.main)
		{
			user->worldState.ClearIfWorld(user->worldState.main->worldId);
		}

		return user->worldState;
	}

	WorldTransitionState& ServerWorldTransitionCoordinator::BeginTransition(UserContext& user, WorldTransitionState transition, eWorldTransitionPhase initialPhase)
	{
		transition.phase = initialPhase;
		user.worldTransition.active = std::move(transition);
		return *user.worldTransition.active;
	}

	bool ServerWorldTransitionCoordinator::AdvanceTransition(UserContext& user, eWorldTransitionPhase nextPhase)
	{
		JAM_ASSERT(user.worldTransition.active);
		auto& transition = *user.worldTransition.active;
		transition.phase = nextPhase;

		const WorldTransitionContinuation continuation{ transition.userId, transition.token, transition.phase };
		switch (nextPhase)
		{
		case eWorldTransitionPhase::ReservingTarget:
		{
			const auto token  = transition.token;
			const auto userId = transition.userId;
			const auto target = *transition.target;
			return SubmitWorldCommand(target, continuation, [token, userId, target](WorldShardState& state)
				{
					if (!state.ReserveEnter(token, userId, target))
						return false;
					if (state.PrepareEnter(token))
						return true;
					state.RollbackEnter(token);
					return false;
				});
		}
		case eWorldTransitionPhase::DetachingSource:
		{
			const auto token  = transition.token;
			const auto userId = transition.userId;
			const auto source = *transition.source;
			return SubmitWorldCommand(source, continuation, [token, userId, source](WorldShardState& state)
				{
					if (!state.PrepareLeave(token, userId, source))
						return false;
					if (state.DetachMain(token))
						return true;
					state.CancelPreparedLeave(token);
					return false;
				});
		}
		case eWorldTransitionPhase::AttachingTarget:
		{
			const WorldUserContext worldUser{
				.accountId	  = user.accountId,
				.userId		  = transition.userId,
				.mainRevision = transition.expectedRevision + 1,
				.sessions	  = ResolveUserSessionBundle(user),
			};
			const auto token = transition.token;
			return SubmitWorldCommand(*transition.target, continuation, [token, worldUser](WorldShardState& state)
				{ return state.AttachPrepared(token, worldUser); });
		}
		case eWorldTransitionPhase::PreparingTargetContent:
		{
			const ServerWorldMemberContentContext context{
				.accountId		 = user.accountId,
				.userId			 = transition.userId,
				.transitionToken = transition.token,
				.correlation	 = {
					.world		  = *transition.target,
					.mainRevision = transition.expectedRevision + 1,
				},
				.entryPoint = transition.contentEntryPoint,
			};
			const auto weak = weak_from_this();
			return SubmitWorldJob(*transition.target, [continuation, context, weak](WorldBase& base)
				{
					auto* world = dynamic_cast<ServerWorld*>(&base);
					if (!world) return;

					world->PrepareMemberContent(context, [continuation, weak](bool succeeded)
						{
							SubmitToUserShard(continuation.userId, Job([continuation, succeeded, weak]()
								{
									if (const auto coordinator = weak.lock())
										coordinator->OnWorldCommandCompleted(continuation, succeeded);
								}, eJobPriority::Control));
						});
				}, eJobPriority::Control);
		}
		case eWorldTransitionPhase::ActivatingTarget:
		{
			const auto token = transition.token;
			return SubmitWorldCommand(*transition.target, continuation, [token](WorldShardState& state)
				{ return state.ActivateAttached(token); });
		}
		case eWorldTransitionPhase::CommittingSourceLeave:
		{
			const auto token = transition.token;
			return SubmitWorldCommand(*transition.source, continuation, [token](WorldShardState& state)
				{ return state.CommitLeave(token); });
		}
		case eWorldTransitionPhase::RollingBackTarget:
		{
			const auto token = transition.token;
			return SubmitWorldCommand(*transition.target, continuation, [token](WorldShardState& state)
				{ return state.RollbackEnter(token); });
		}
		case eWorldTransitionPhase::RestoringSource:
		{
			const WorldUserContext worldUser{
				.accountId	  = user.accountId,
				.userId		  = transition.userId,
				.mainRevision = user.worldState.revision,
				.sessions	  = ResolveUserSessionBundle(user),
			};
			const auto token = transition.token;
			return SubmitWorldCommand(*transition.source, continuation, [token, worldUser](WorldShardState& state)
				{ return state.RestoreDetachedMain(token, worldUser); });
		}
		case eWorldTransitionPhase::WaitingClientPrepared:
			if (transition.terminalFailure != eWorldTransitionFailure::None)
				return true;

			SendToUser(transition.userId, codec::MakeClientWorldPreparePacket(
				{
					.token			= transition.syncToken,
					.kind			= eWorldSyncKind::WorldPrepare,
					.correlation	= { .world = *transition.target, .mainRevision = transition.expectedRevision },
					.archetypeKey	= transition.target ? transition.target->instance.archetypeKey : WorldArchetypeKey{},
				}), eProtocolType::TCP);

			return true;
		case eWorldTransitionPhase::WaitingClientApplied:
			if (transition.terminalFailure != eWorldTransitionFailure::None)
				return true;
			SendToUser(transition.userId, codec::MakeClientWorldCommitPacket(
				{
					.token		   = transition.syncToken,
					.correlation = { .world = *transition.target, .mainRevision = user.worldState.revision },
				}), eProtocolType::TCP);
			return true;
		case eWorldTransitionPhase::CommittingMain:
			if (transition.kind == eWorldTransitionKind::Leave)
			{
				if (!CommitMain(transition.userId, transition.expectedRevision, std::nullopt))
					return false;
				return AdvanceTransition(user, eWorldTransitionPhase::CommittingSourceLeave);
			}
			if (!CommitMain(transition.userId, transition.expectedRevision, transition.target))
				return false;
			return AdvanceTransition(user, eWorldTransitionPhase::WaitingClientApplied);
		default:
			return true;
		}
	}

	void ServerWorldTransitionCoordinator::FailTransition(UserContext& user, eWorldTransitionFailure failure, bool commandInFlight)
	{
		auto& transition = *user.worldTransition.active;
		transition.terminalFailure = failure;

		if (transition.phase == eWorldTransitionPhase::RollingBackTarget || transition.phase == eWorldTransitionPhase::RestoringSource)
			return;
		if (commandInFlight && (transition.phase == eWorldTransitionPhase::ReservingTarget
			|| transition.phase == eWorldTransitionPhase::DetachingSource
			|| transition.phase == eWorldTransitionPhase::AttachingTarget
			|| transition.phase == eWorldTransitionPhase::PreparingTargetContent
			|| transition.phase == eWorldTransitionPhase::ActivatingTarget
			|| transition.phase == eWorldTransitionPhase::CommittingSourceLeave))
			return;
		if (transition.phase == eWorldTransitionPhase::ResolvingTarget
			|| transition.phase == eWorldTransitionPhase::ReservingTarget)
		{
			FinishFailure(user);
			return;
		}

		if (transition.kind == eWorldTransitionKind::Enter && transition.target)
		{
			if (user.worldState.main && *user.worldState.main == *transition.target)
				CommitMain(transition.userId, user.worldState.revision, transition.source);

			if (!AdvanceTransition(user, eWorldTransitionPhase::RollingBackTarget))
			{
				if (transition.sourceDetached && transition.source
					&& AdvanceTransition(user, eWorldTransitionPhase::RestoringSource))
					return;
				FinishFailure(user);
			}
			return;
		}

		if (transition.sourceDetached && transition.source)
		{
			if (!user.worldState.main)
				CommitMain(transition.userId, user.worldState.revision, transition.source);

			if (AdvanceTransition(user, eWorldTransitionPhase::RestoringSource))
				return;
		}
		FinishFailure(user);
	}





	WorldTransitionToken ServerWorldTransitionCoordinator::IssueTransitionToken()
	{
		for (;;)
		{
			const uint64 value = m_nextTransitionToken.fetch_add(1, std::memory_order_relaxed);
			if (value != 0)
				return WorldTransitionToken{ value };
		}
	}

	WorldSyncToken ServerWorldTransitionCoordinator::IssueWorldSyncToken()
	{
		for (;;)
		{
			const uint64 value = m_nextWorldSyncToken.fetch_add(1, std::memory_order_relaxed);
			if (value != 0)
				return WorldSyncToken{ value };
		}
	}

	std::optional<WorldInstanceRef> ServerWorldTransitionCoordinator::ResolveDestination(const EnterWorldRequest& request) const
	{
		const WorldInstanceDefinition* definition = nullptr;
		switch (request.selector)
		{
		case eWorldDestinationSelector::DefaultForArchetype:
			definition = m_instancesDB.FindDefault(request.archetypeKey);
			break;
		case eWorldDestinationSelector::ExplicitInstance:
			definition = m_instancesDB.Find(request.explicitInstanceId);
			break;
		case eWorldDestinationSelector::AuthoredDestination:
			definition = m_instancesDB.Find(request.destinationName);
			break;
		}

		if (!definition || definition->archetypeKey != request.archetypeKey)
			return std::nullopt;
		return definition->Ref();
	}

	void ServerWorldTransitionCoordinator::EnsureWorldAsync(const WorldInstanceRef& instance, WorldReadyFn completed)
	{
		if (!completed)
			return;

		bool startCreation = false;
		std::optional<WorldRef> readyWorld;
		bool invalidInstance = false;
		{
			std::scoped_lock lock(m_worldMutex);
			auto it = m_worldSlots.find(instance.instanceId.value);
			if (it == m_worldSlots.end())
			{
				WorldSlot slot{};
				slot.instance = instance;
				slot.waiters.push_back(std::move(completed));
				m_worldSlots.emplace(instance.instanceId.value, std::move(slot));
				startCreation = true;
			}
			else if (it->second.instance != instance)
			{
				invalidInstance = true;
			}
			else if (it->second.state == eWorldSlotState::Ready && it->second.world)
			{
				readyWorld = it->second.world;
			}
			else
			{
				it->second.waiters.push_back(std::move(completed));
			}
		}
		if (invalidInstance)
		{
			completed(std::nullopt);
			return;
		}
		if (readyWorld)
		{
			completed(*readyWorld);
			return;
		}
		if (startCreation)
			StartWorldCreation(instance);
	}

	void ServerWorldTransitionCoordinator::StartWorldCreation(const WorldInstanceRef& instance)
	{
		{
			std::scoped_lock lock(m_worldMutex);
			const auto it = m_worldSlots.find(instance.instanceId.value);
			if (it == m_worldSlots.end() || it->second.instance != instance
				|| it->second.state != eWorldSlotState::Creating)
			{
				return;
			}
		}

		const auto* definition = m_instancesDB.Find(instance.instanceId);
		if (!m_owner || !m_configResolver || !definition || definition->Ref() != instance)
		{
			CompleteWorldCreation(instance, std::nullopt);
			return;
		}

		WorldConfig config = m_configResolver->ResolveWorldConfig(instance);
		if (!config.IsValid())
		{
			CompleteWorldCreation(instance, std::nullopt);
			return;
		}

		RoutePlacementOptions placement{};
		if (config.templateData.route.preferredShard != 0)
		{
			placement.affinity.preferredShard = config.templateData.route.preferredShard;
			placement.affinity.hard			  = config.templateData.route.hardAffinity;
		}
		const RouteAssignment route = GLOBAL_EXEC.PlaceRoute(GLOBAL_EXEC.MakeRouteKey("WorldInstance", instance.instanceId.value), placement);

		auto shard = GLOBAL_EXEC.GetShard(route);
		if (!shard)
		{
			CompleteWorldCreation(instance, std::nullopt);
			return;
		}

		const std::weak_ptr<ServerWorldTransitionCoordinator> weak = weak_from_this();

		shard->Submit(Job([instance, route, config = std::move(config), weak]() mutable
			{
				if (const auto coordinator = weak.lock())
				{
					if (!coordinator->m_owner)
					{
						coordinator->CompleteWorldCreation(instance, std::nullopt);
						return;
					}

					auto record = CreateWorldOnShard(GetOrCreateWorldShardState(CurrentShardLocalChecked()), route, config,
							coordinator->m_actorArchetypesDB,
							*coordinator->m_owner, weak);
					coordinator->CompleteWorldCreation(instance, record);
				}
			}, eJobPriority::Control));
	}

	void ServerWorldTransitionCoordinator::CompleteWorldCreation(const WorldInstanceRef& instance, std::optional<WorldRecord> record)
	{
		std::vector<WorldReadyFn> waiters;
		std::optional<WorldRef> world;
		{
			std::scoped_lock lock(m_worldMutex);
			auto it = m_worldSlots.find(instance.instanceId.value);
			if (it == m_worldSlots.end() || it->second.instance != instance
				|| it->second.state != eWorldSlotState::Creating)
			{
				return;
			}

			waiters = std::move(it->second.waiters);
			if (record && record->world.IsValid() && record->world.instance == instance)
			{
				it->second.state = eWorldSlotState::Ready;
				it->second.world = record->world;
				world = record->world;
			}
			else
				m_worldSlots.erase(it);
		}
		for (auto& waiter : waiters)
		{
			if (waiter)
				waiter(world);
		}
	}

	void ServerWorldTransitionCoordinator::CompleteWorldDestruction(const WorldRef& world, bool destroyed)
	{
		std::vector<WorldReadyFn> readyWaiters;
		bool restartCreation = false;
		{
			std::scoped_lock lock(m_worldMutex);
			auto it = m_worldSlots.find(world.instance.instanceId.value);
			if (it == m_worldSlots.end() || it->second.instance != world.instance
				|| it->second.world != world || it->second.state != eWorldSlotState::Destroying)
			{
				return;
			}

			if (!destroyed)
			{
				it->second.state = eWorldSlotState::Ready;
				readyWaiters = std::move(it->second.waiters);
			}
			else if (it->second.waiters.empty())
			{
				m_worldSlots.erase(it);
			}
			else
			{
				it->second.state = eWorldSlotState::Creating;
				it->second.world.reset();
				restartCreation = true;
			}
		}

		for (auto& waiter : readyWaiters)
			if (waiter)
				waiter(world);

		if (restartCreation)
			StartWorldCreation(world.instance);
	}

	bool ServerWorldTransitionCoordinator::SubmitWorldCommand(const WorldRef& world,
		const WorldTransitionContinuation& continuation, std::function<bool(WorldShardState&)> command)
	{
		if (!world.IsValid() || !command)
			return false;

		const std::weak_ptr<ServerWorldTransitionCoordinator> weak = weak_from_this();
		return SubmitToWorldShard(world.worldId, Job([worldId = world.worldId, continuation, command = std::move(command), weak]() mutable
			{
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				const bool succeeded = state.FindAuthoritativeWorldEntry(worldId) && command(state);
				auto userShard = GLOBAL_EXEC.GetShardFromIndex(GetUserShardIndex(continuation.userId));
				if (!userShard)
					return;
				userShard->Submit(Job([continuation, succeeded, weak]()
					{
						if (const auto coordinator = weak.lock())
							coordinator->OnWorldCommandCompleted(continuation, succeeded);
					}, eJobPriority::Control));
			}, eJobPriority::Control));
	}

	void ServerWorldTransitionCoordinator::EnterOnUserShard(UserId userId, const EnterWorldRequest& request, uint64 nowNs)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user)
		{
			JAM_LOG_WARN("World transition requested for missing user. userId={}", userId);
			return;
		}

		if (!m_owner || !request.IsValid() || user->worldTransition.active)
		{
			SendToUser(userId, codec::MakeWorldTransitionResultPacket(
				{ 
					.kind		= eWorldTransitionKind::Enter, 
					.requestId	= request.requestId, 
					.failure	= eWorldTransitionFailure::InvalidRequest 
				}), eProtocolType::TCP);
			return;
		}

		const UserWorldState current = user->worldState;
		if (current.revision != request.expectedMainRevision)
		{
			SendToUser(userId, codec::MakeWorldTransitionResultPacket(
				{ 
					.kind		= eWorldTransitionKind::Enter, 
					.requestId	= request.requestId, 
					.failure	= eWorldTransitionFailure::StaleRevision, 
					.state		= current 
				}), eProtocolType::TCP);
			return;
		}

		EnterWorldRequest resolvedRequest = request;
		if (resolvedRequest.IsServerResolved())
		{
			const auto archetypeKey = m_owner->ResolveEnterWorldDestination(user->accountId, userId);
			if (archetypeKey)
				resolvedRequest.archetypeKey = *archetypeKey;
		}

		const auto instance = ResolveDestination(resolvedRequest);
		if (!instance)
		{
			SendToUser(userId, codec::MakeWorldTransitionResultPacket(
				{ 
					.kind		= eWorldTransitionKind::Enter, 
					.requestId	= request.requestId, 
					.failure	= eWorldTransitionFailure::DestinationUnavailable, 
					.state		= current 
				}), eProtocolType::TCP);
			return;
		}

		WorldTransitionState transition = {};
		transition.token				= IssueTransitionToken();
		transition.kind					= eWorldTransitionKind::Enter;
		transition.userId				= userId;
		transition.requestId			= request.requestId;
		transition.source				= current.main;
		transition.targetInstance		= *instance;
		transition.expectedRevision		= current.revision;
		transition.contentEntryPoint	= request.contentEntryPoint;
		transition.syncToken			= IssueWorldSyncToken();
		transition.deadlineNs			= nowNs + m_worldSyncTimeoutNs;

		auto& active = BeginTransition(*user, std::move(transition), eWorldTransitionPhase::ResolvingTarget);

		const WorldTransitionContinuation continuation{ userId, active.token, active.phase };
		const std::weak_ptr<ServerWorldTransitionCoordinator> weak = weak_from_this();
		EnsureWorldAsync(*instance, [weak, continuation](std::optional<WorldRef> world)
			{
				if (const auto coordinator = weak.lock())
				{
					SubmitToUserShard(continuation.userId, Job([weak, continuation, world]()
						{
							if (const auto resumed = weak.lock())
								resumed->OnWorldReady(continuation, world);
						}, eJobPriority::Control));
				}
			});
	}

	void ServerWorldTransitionCoordinator::LeaveOnUserShard(UserId userId, const LeaveWorldRequest& request)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user)
		{
			JAM_LOG_WARN("World transition requested for missing user. userId={}", userId);
			return;
		}

		const auto current = user->worldState;
		if (!m_owner || user->worldTransition.active || current.revision != request.expectedMainRevision)
		{
			SendToUser(userId, codec::MakeWorldTransitionResultPacket(
				{
					.kind		= eWorldTransitionKind::Leave,
					.requestId	= request.requestId,
					.failure	= current.revision != request.expectedMainRevision ? eWorldTransitionFailure::StaleRevision : eWorldTransitionFailure::InvalidRequest,
					.state		= current
				}), eProtocolType::TCP);
			return;
		}
		if (!current.main)
		{
			SendToUser(userId, codec::MakeWorldTransitionResultPacket(
				{
					.kind		= eWorldTransitionKind::Leave,
					.requestId	= request.requestId,
					.state		= current
				}), eProtocolType::TCP);
			return;
		}

		WorldTransitionState transition
		{
			.token				= IssueTransitionToken(),
			.kind				= eWorldTransitionKind::Leave,
			.userId				= userId,
			.requestId			= request.requestId,
			.source				= current.main,
			.expectedRevision	= current.revision,
			.deadlineNs			= NOW_NS() + m_worldSyncTimeoutNs,
		};
		BeginTransition(*user, std::move(transition), eWorldTransitionPhase::DetachingSource);

		if (!AdvanceTransition(*user, eWorldTransitionPhase::DetachingSource))
			FailTransition(*user, eWorldTransitionFailure::WorldDestroyed);
	}

	void ServerWorldTransitionCoordinator::OnWorldReady(const WorldTransitionContinuation& continuation, std::optional<WorldRef> world)
	{
		ReduceTransition(WorldReadyEvent{ continuation, world });
	}


	void ServerWorldTransitionCoordinator::OnClientWorldSyncResult(uint64 userId, const ClientWorldSyncResult& result, uint64 nowNs)
	{
		if (UserContext* resyncUser = FindUserOnCurrentShard(userId);
			resyncUser && resyncUser->worldResync.phase != eUserWorldResyncPhase::None
			&& resyncUser->worldResync.token == result.token)
		{
			auto& resync = resyncUser->worldResync;
			if (!result.succeeded || resync.deadlineNs <= nowNs)
			{
				FailWorldResync(*resyncUser);
				return;
			}

			if (resync.phase == eUserWorldResyncPhase::WaitingClientPrepared)
			{
				resync.phase = eUserWorldResyncPhase::WaitingClientApplied;
				SendToUser(userId, codec::MakeClientWorldCommitPacket({
					.token = resync.token,
					.correlation = { .world = resync.world, .mainRevision = resync.mainRevision },
				}), eProtocolType::TCP);
				return;
			}

			if (resync.phase == eUserWorldResyncPhase::WaitingClientApplied)
			{
				resync.phase = eUserWorldResyncPhase::WaitingTransport;
				TryCompleteWorldResync(*resyncUser);
			}
			return;
		}

		ReduceTransition(ClientWorldSyncCompletedEvent{ userId, result, nowNs });
	}

	void ServerWorldTransitionCoordinator::OnWorldCommandCompleted(const WorldTransitionContinuation& continuation, bool succeeded)
	{
		ReduceTransition(WorldCommandCompletedEvent{ continuation, succeeded });
	}

	void ServerWorldTransitionCoordinator::ReduceTransition(const WorldTransitionEvent& event)
	{
		if (const auto* ready = std::get_if<WorldReadyEvent>(&event))
		{
			UserContext* user = FindExpectedTransition(ready->continuation);
			if (!user) return;

			auto& transition = *user->worldTransition.active;
			if (!ready->world || ready->world->instance != transition.targetInstance)
			{
				FailTransition(*user, eWorldTransitionFailure::DestinationUnavailable);
				return;
			}

			//JAMNET_LOG_INFO("[EnterWorld] world ready. userId={}, token={}, worldId={}", transition.userId, transition.token.value, ready->world->worldId);
			if (transition.source && *transition.source == *ready->world)
			{
				CompleteEnter(*user, user->worldState);
				return;
			}
			transition.target = *ready->world;
			if (!AdvanceTransition(*user, eWorldTransitionPhase::ReservingTarget))
				FailTransition(*user, eWorldTransitionFailure::DestinationUnavailable);
			return;
		}

		if (const auto* sync = std::get_if<ClientWorldSyncCompletedEvent>(&event))
		{
			UserContext* user = FindUserOnCurrentShard(sync->userId);
			if (!user || !user->worldTransition.active)
				return;

			const WorldTransitionState& transition = *user->worldTransition.active;
			if ((transition.phase != eWorldTransitionPhase::WaitingClientApplied
				&& transition.phase != eWorldTransitionPhase::WaitingClientPrepared)
				|| transition.syncToken != sync->result.token)
				return;
			if (transition.deadlineNs <= sync->nowNs)
			{
				FailTransition(*user, eWorldTransitionFailure::Timeout);
				return;
			}
			if (transition.phase == eWorldTransitionPhase::WaitingClientApplied)
			{
				if (sync->result.succeeded)
				{
					if (!AdvanceTransition(*user, eWorldTransitionPhase::ActivatingTarget))
						FailTransition(*user, eWorldTransitionFailure::InternalError);
				}
				else
					FailTransition(*user, sync->result.failure == eWorldTransitionFailure::None ? eWorldTransitionFailure::ClientPrepareFailed : sync->result.failure);
				return;
			}
			if (!sync->result.succeeded)
			{
				FailTransition(*user, sync->result.failure == eWorldTransitionFailure::None ? eWorldTransitionFailure::ClientPrepareFailed : sync->result.failure);
				return;
			}
			if (transition.source)
			{
				if (!AdvanceTransition(*user, eWorldTransitionPhase::DetachingSource))
					FailTransition(*user, eWorldTransitionFailure::WorldDestroyed);
			}
			else if (!AdvanceTransition(*user, eWorldTransitionPhase::AttachingTarget))
			{
				FailTransition(*user, eWorldTransitionFailure::InternalError);
			}
			return;
		}

		if (const auto* timedOut = std::get_if<TransitionTimedOutEvent>(&event))
		{
			JAM_ASSERT(CurrentShardLocal() && CurrentShardLocal()->shardIndex == GetUserShardIndex(timedOut->continuation.userId));
			UserContext* user = FindExpectedTransition(timedOut->continuation);
			if (!user || user->worldTransition.active->deadlineNs > timedOut->nowNs)
				return;

			FailTransition(*user, eWorldTransitionFailure::Timeout, true);
			return;
		}

		if (const auto* disconnected = std::get_if<UserDisconnectedEvent>(&event))
		{
			JAM_ASSERT(CurrentShardLocal() && CurrentShardLocal()->shardIndex == GetUserShardIndex(disconnected->continuation.userId));
			UserContext* user = FindExpectedTransition(disconnected->continuation);
			if (!user)
				return;

			FailTransition(*user, eWorldTransitionFailure::Disconnected, true);
			return;
		}

		const auto& completed = std::get<WorldCommandCompletedEvent>(event);
		const auto& continuation = completed.continuation;
		const bool succeeded = completed.succeeded;

		UserContext* user = FindExpectedTransition(continuation);
		if (!user) return;

		auto& transition = *user->worldTransition.active;
		if (transition.terminalFailure != eWorldTransitionFailure::None)
		{
			if ((continuation.expectedPhase == eWorldTransitionPhase::ActivatingTarget
				|| continuation.expectedPhase == eWorldTransitionPhase::CommittingSourceLeave) && succeeded)
			{
				transition.terminalFailure = eWorldTransitionFailure::None;
			}
			else
			{
				if (continuation.expectedPhase == eWorldTransitionPhase::DetachingSource && succeeded)
					transition.sourceDetached = true;

				const eWorldTransitionFailure failure = transition.terminalFailure;
				AdvanceTransition(*user, eWorldTransitionPhase::WaitingClientPrepared);
				FailTransition(*user, failure);
				return;
			}
		}
		switch (continuation.expectedPhase)
		{
		case eWorldTransitionPhase::ReservingTarget:
			if (!succeeded)
			{
				FailTransition(*user, eWorldTransitionFailure::CapacityExceeded);
			}
			else
			{
				//JAMNET_LOG_INFO("[EnterWorld] target reserved. userId={}, token={}; sending prepare", transition.userId, transition.token.value);
				AdvanceTransition(*user, eWorldTransitionPhase::WaitingClientPrepared);
			}
			return;

		case eWorldTransitionPhase::DetachingSource:
			if (!succeeded)
			{
				FailTransition(*user, eWorldTransitionFailure::WorldDestroyed);
				return;
			}
			transition.sourceDetached = true;
			
			if (transition.kind == eWorldTransitionKind::Leave)
			{
				if (!AdvanceTransition(*user, eWorldTransitionPhase::CommittingMain))
					FailTransition(*user, eWorldTransitionFailure::InternalError);
			}
			else if (!AdvanceTransition(*user, eWorldTransitionPhase::AttachingTarget))
			{
				FailTransition(*user, eWorldTransitionFailure::InternalError);
			}
			
			return;

		case eWorldTransitionPhase::AttachingTarget:
			if (!succeeded)
			{
				FailTransition(*user, eWorldTransitionFailure::InternalError);
				return;
			}
			if (!AdvanceTransition(*user, eWorldTransitionPhase::PreparingTargetContent))
				FailTransition(*user, eWorldTransitionFailure::InternalError);
			return;

		case eWorldTransitionPhase::PreparingTargetContent:
			if (!succeeded)
			{
				FailTransition(*user, eWorldTransitionFailure::InternalError);
				return;
			}
			if (!AdvanceTransition(*user, eWorldTransitionPhase::CommittingMain))
				FailTransition(*user, eWorldTransitionFailure::InternalError);

			return;

		case eWorldTransitionPhase::ActivatingTarget:
			if (!succeeded)
			{
				FailTransition(*user, eWorldTransitionFailure::InternalError);
				return;
			}
			if (transition.source)
			{
				if (!AdvanceTransition(*user, eWorldTransitionPhase::CommittingSourceLeave))
					FailTransition(*user, eWorldTransitionFailure::InternalError);
			}
			else
			{
				CompleteEnter(*user, user->worldState);
			}
			return;

		case eWorldTransitionPhase::CommittingSourceLeave:
			if (transition.kind == eWorldTransitionKind::Enter)
			{
				if (!succeeded)
					JAM_LOG_WARN("Source leave commit failed after target activation. userId={}, token={}", transition.userId, transition.token.value);
				CompleteEnter(*user, user->worldState);
			}
			else if (succeeded)
			{
				CompleteLeave(*user, user->worldState);
			}
			else
			{
				FailTransition(*user, eWorldTransitionFailure::InternalError);
			}
			return;

		case eWorldTransitionPhase::RollingBackTarget:
			if (transition.sourceDetached && transition.source)
			{
				if (!AdvanceTransition(*user, eWorldTransitionPhase::RestoringSource))
					FinishFailure(*user);
			}
			else
			{
				FinishFailure(*user);
			}
			return;

		case eWorldTransitionPhase::RestoringSource:
			FinishFailure(*user);
			return;

		default:
			return;
		}
	}

	void ServerWorldTransitionCoordinator::CompleteEnter(UserContext& user, const UserWorldState& state)
	{
		const WorldTransitionState transition = *user.worldTransition.active;
		user.worldTransition.active.reset();

		SendToUser(transition.userId, codec::MakeWorldTransitionResultPacket(
			{ 
				.kind			 = eWorldTransitionKind::Enter, 
				.requestId		 = transition.requestId, 
				.transitionToken = transition.token, 
				.state			 = state 
			}), eProtocolType::TCP);

		SendToUser(transition.userId, codec::MakeUserMainWorldChangedPacket(state), eProtocolType::TCP);
		
		if (user.connectionState == eUserConnectionState::Connected && user.worldResync.phase == eUserWorldResyncPhase::WaitingTransition)
			BeginWorldResync(user);
	}

	void ServerWorldTransitionCoordinator::CompleteLeave(UserContext& user, const UserWorldState& state)
	{
		const WorldTransitionState transition = *user.worldTransition.active;
		user.worldTransition.active.reset();

		SendToUser(transition.userId, codec::MakeWorldTransitionResultPacket(
			{ 
				.kind			 = eWorldTransitionKind::Leave, 
				.requestId		 = transition.requestId, 
				.transitionToken = transition.token, 
				.state			 = state 
			}), eProtocolType::TCP);
		
		SendToUser(transition.userId, codec::MakeUserMainWorldChangedPacket(state), eProtocolType::TCP);
		
		if (user.connectionState == eUserConnectionState::Connected && user.worldResync.phase == eUserWorldResyncPhase::WaitingTransition)
			BeginWorldResync(user);
		
		if (user.connectionState == eUserConnectionState::Released && user.tcp == kInvalidSessionId && user.udp == kInvalidSessionId && !user.worldState.main)
			GetOrCreateUserShardState(CurrentShardLocalChecked()).FreeUserContext(transition.userId);
	}

	

	void ServerWorldTransitionCoordinator::FinishFailure(UserContext& user)
	{
		const WorldTransitionState transition = *user.worldTransition.active;
		const UserWorldState state = user.worldState;
		user.worldTransition.active.reset();

		SendToUser(transition.userId, codec::MakeWorldTransitionResultPacket(
			{
				.kind			 = transition.kind, 
				.requestId		 = transition.requestId,
				.transitionToken = transition.token, 
				.failure		 = transition.terminalFailure, 
				.state			 = state 
			}), eProtocolType::TCP);

		if (transition.kind == eWorldTransitionKind::Enter)
			JAM_LOG_WARN("[EnterWorld] failed. userId={}, requestId={}, token={}, failure={}", transition.userId, transition.requestId, transition.token.value, static_cast<uint32>(transition.terminalFailure));

		if (user.connectionState == eUserConnectionState::Connected
			&& user.worldResync.phase == eUserWorldResyncPhase::WaitingTransition)
		{
			BeginWorldResync(user);
		}

		if (user.connectionState == eUserConnectionState::Released
			&& user.tcp == kInvalidSessionId && user.udp == kInvalidSessionId && !user.worldState.main)
			GetOrCreateUserShardState(CurrentShardLocalChecked()).FreeUserContext(transition.userId);
	}

	void ServerWorldTransitionCoordinator::Tick(uint64 nowNs)
	{
		auto* local = CurrentShardLocal();
		if (!local)
			return;
		auto& state = GetOrCreateUserShardState(*local);
		for (auto& entry : state.usersById.entries)
		{
			if (!entry.occupied)
				continue;

			if (entry.value.worldTransition.active)
			{
				const auto& transition = *entry.value.worldTransition.active;
				if (transition.deadlineNs <= nowNs)
				{
					ReduceTransition(TransitionTimedOutEvent{
						.continuation = { transition.userId, transition.token, transition.phase },
						.nowNs = nowNs,
					});
				}
			}

			if (entry.occupied && entry.value.worldResync.phase != eUserWorldResyncPhase::None
				&& entry.value.worldResync.deadlineNs <= nowNs)
			{
				FailWorldResync(entry.value);
			}

			if (entry.occupied
				&& entry.value.connectionState == eUserConnectionState::Reconnecting
				&& entry.value.reconnectDeadlineNs <= nowNs)
			{
				ReleaseReconnectingUser(entry.value);
			}
		}
	}

	void ServerWorldTransitionCoordinator::OnDisconnected(uint64 userId)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user || user->connectionState == eUserConnectionState::Released)
			return;

		user->connectionState     = eUserConnectionState::Reconnecting;
		user->reconnectDeadlineNs = NOW_NS() + m_reconnectTimeoutNs;
		user->worldResync		  = {};

		if (!m_owner)
		{
			ReleaseReconnectingUser(*user);
			return;
		}

		if (user->worldTransition.active)
		{
			const auto& transition = *user->worldTransition.active;
			ReduceTransition(UserDisconnectedEvent{
				.continuation = { transition.userId, transition.token, transition.phase },
			});
		}

		user = FindUserOnCurrentShard(userId);
		if (user)
		{
			SuspendWorldUser(*user);
			RefreshWorldUserContext(*user);
		}
	}

	void ServerWorldTransitionCoordinator::OnReconnected(uint64 userId)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user || user->connectionState == eUserConnectionState::Released)
			return;

		// A reconnect is transport-ready only after both the replacement TCP and UDP
		// sessions have been registered. Keep the reconnect timeout active until then.
		OnSessionChanged(userId);
	}

	void ServerWorldTransitionCoordinator::OnSessionChanged(uint64 userId)
	{
		if (UserContext* user = FindUserOnCurrentShard(userId))
		{
			if (user->connectionState == eUserConnectionState::Reconnecting)
			{
				if (user->tcp == kInvalidSessionId || user->udp == kInvalidSessionId)
					return;

				user->connectionState	  = eUserConnectionState::Connected;
				user->reconnectDeadlineNs = 0;

				RefreshWorldUserContext(*user);
				BeginWorldResync(*user);
				
				return;
			}

			if (user->connectionState != eUserConnectionState::Connected)
				return;

			RefreshWorldUserContext(*user);
			TryCompleteWorldResync(*user);
		}
	}

	void ServerWorldTransitionCoordinator::RefreshWorldUserContext(const UserContext& user)
	{
		if (!user.worldState.main)
			return;

		const WorldUserContext worldUser{
			.accountId		= user.accountId,
			.userId			= user.userId,
			.mainRevision	= user.worldState.revision,
			.sessions		= ResolveUserSessionBundle(user),
		};
		SubmitWorldJob(*user.worldState.main, [worldUser](WorldBase& world) mutable
			{
				if (auto* host = dynamic_cast<WorldMembershipHost*>(&world))
					host->UpdateMemberContext(worldUser);
			});
	}

	void ServerWorldTransitionCoordinator::SuspendWorldUser(const UserContext& user)
	{
		if (!user.worldState.main)
			return;

		SubmitWorldJob(*user.worldState.main, [userId = user.userId](WorldBase& world)
			{
				if (auto* serverWorld = dynamic_cast<ServerWorld*>(&world))
					serverWorld->SuspendMemberReplication(userId);
			});
	}

	void ServerWorldTransitionCoordinator::BeginWorldResync(UserContext& user)
	{
		if (user.connectionState != eUserConnectionState::Connected)
			return;
		if (!user.worldState.main)
		{
			user.worldResync = {};
			return;
		}
		if (user.worldTransition.active)
		{
			user.worldResync = {
				.phase = eUserWorldResyncPhase::WaitingTransition,
				.deadlineNs = NOW_NS() + m_worldSyncTimeoutNs,
			};
			return;
		}

		const WorldRef  world = *user.worldState.main;
		const WorldSyncToken token = IssueWorldSyncToken();
		user.worldResync = {
			.phase		  = eUserWorldResyncPhase::WaitingClientPrepared,
			.token		  = token,
			.world		  = world,
			.mainRevision = user.worldState.revision,
			.deadlineNs	  = NOW_NS() + m_worldSyncTimeoutNs,
		};

		SuspendWorldUser(user);
		SendToUser(user.userId, codec::MakeClientWorldPreparePacket({
			.token		  = token,
			.kind		  = eWorldSyncKind::WorldResync,
			.correlation  = { .world = world, .mainRevision = user.worldState.revision },
			.archetypeKey = world.instance.archetypeKey,
		}), eProtocolType::TCP);
	}

	void ServerWorldTransitionCoordinator::TryCompleteWorldResync(UserContext& user)
	{
		if (user.worldResync.phase != eUserWorldResyncPhase::WaitingTransport
			|| user.connectionState != eUserConnectionState::Connected
			|| user.udp == kInvalidSessionId || !user.worldState.main
			|| *user.worldState.main != user.worldResync.world
			|| user.worldState.revision != user.worldResync.mainRevision)
		{
			return;
		}

		const UserId userId = user.userId;
		const WorldRef world = user.worldResync.world;
		if (!SubmitWorldJob(world, [userId](WorldBase& base)
			{
				if (auto* serverWorld = dynamic_cast<ServerWorld*>(&base))
					serverWorld->ResumeMemberReplication(userId);
			}))
		{
			return;
		}

		user.worldResync = {};
	}

	void ServerWorldTransitionCoordinator::FailWorldResync(UserContext& user)
	{
		JAM_LOG_WARN("[WorldResync] failed or timed out. userId={}, token={}, phase={}", user.userId, user.worldResync.token.value, static_cast<uint32>(user.worldResync.phase));
		
		user.worldResync = {};
		const ServerSessionBundle sessions = ResolveUserSessionBundle(user);
		const SessionRef<ServerTcpSession> tcpRef = sessions.tcp;
		if (!tcpRef.TryPost(Job([tcpRef]()
			{
				if (auto* tcp = tcpRef.TryGet(); tcp && !tcp->IsClosing())
					tcp->Disconnect();
			}, eJobPriority::Control)))
		{
			user.tcp = kInvalidSessionId;
			user.udp = kInvalidSessionId;
			OnDisconnected(user.userId);
		}
	}

	void ServerWorldTransitionCoordinator::ReleaseReconnectingUser(UserContext& user)
	{
		if (user.connectionState != eUserConnectionState::Reconnecting)
			return;

		const UserId userId = user.userId;
		user.connectionState = eUserConnectionState::Released;
		user.reconnectDeadlineNs = 0;
		GetOrCreateUserShardState(CurrentShardLocalChecked()).ReleaseAccountBinding(userId);
		if (m_owner)
			m_owner->NotifyUserReleased(userId);

		if (user.worldTransition.active)
		{
			const auto& transition = *user.worldTransition.active;
			ReduceTransition(UserDisconnectedEvent{
				.continuation = { transition.userId, transition.token, transition.phase },
			});
		}

		UserContext* current = FindUserOnCurrentShard(userId);
		if (!current)
			return;

		const std::optional<WorldRef> main = current->worldState.main;
		if (!main)
		{
			if (current->tcp == kInvalidSessionId && current->udp == kInvalidSessionId && !current->worldTransition.active)
				GetOrCreateUserShardState(CurrentShardLocalChecked()).FreeUserContext(userId);
			return;
		}

		auto worldShard = GLOBAL_EXEC.GetShardFromIndex(GetWorldShardIndex(main->worldId));
		if (!worldShard)
		{
			CompleteDisconnectedWorldLeave(userId, *main);
			return;
		}

		if (auto* local = CurrentShardLocal(); local && local->shardIndex == GetWorldShardIndex(main->worldId))
		{
			GetOrCreateWorldShardState(*local).DisconnectMember(userId, *main);
			CompleteDisconnectedWorldLeave(userId, *main);
		}
		else
		{
			const std::weak_ptr<ServerWorldTransitionCoordinator> weak = weak_from_this();
			worldShard->Submit(Job([weak, userId, world = *main]()
				{
					GetOrCreateWorldShardState(CurrentShardLocalChecked()).DisconnectMember(userId, world);
					if (const auto coordinator = weak.lock())
					{
						SubmitToUserShard(userId, Job([weak, userId, world]()
							{
								if (const auto resumed = weak.lock())
									resumed->CompleteDisconnectedWorldLeave(userId, world);
							}, eJobPriority::Control));
					}
				}, eJobPriority::Control));
		}
	}

	void ServerWorldTransitionCoordinator::CompleteDisconnectedWorldLeave(uint64 userId, const WorldRef& world)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (user)
			user->worldState.ClearIfWorld(world.worldId);

		if (user && user->connectionState == eUserConnectionState::Released
			&& user->tcp == kInvalidSessionId && user->udp == kInvalidSessionId
			&& !user->worldState.main && !user->worldTransition.active)
			GetOrCreateUserShardState(CurrentShardLocalChecked()).FreeUserContext(userId);
	}
}
