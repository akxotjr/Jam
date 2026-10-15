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
#include "jamnet/runtime/world/lifecycle/ServerWorldSyncSystem.h"

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
						target->Enter(userId, request, NOW());
				});

			world->SetActorArchetypeDatabase(actorArchetypes);
			if (!resolved.actorLevelPath.empty())
				world->SetActorLevelDatabase(ActorLevelsLoader::Load(resolved.actorLevelPath));

			state.RegisterWorld(resolved);
			if (!world->Initialize())
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
			activeWorld->Start(SIMULATION_TICK_NS);

			const WorldRecord* record = state.FindAuthoritativeWorldEntry(worldId);
			return record ? std::optional(*record) : std::nullopt;
		}
	}




	bool ServerWorldTransitionCoordinator::Initialize(ServerNetworkManager* owner)
	{
		m_owner = owner;
		if (!owner || !owner->GetSharedDataManifest())
			return false;

		m_reconnectTimeout = owner->GetReconnectTimeoutNs();

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

	void ServerWorldTransitionCoordinator::Close()
	{
		struct CloseBarrier
		{
			std::mutex mutex;
			std::condition_variable cv;
			size_t remainingShards = 0;
		};

		auto barrier = std::make_shared<CloseBarrier>();
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
					if (!state.BeginDestroyMembershipWorld(world.worldId, eMailboxCloseMode::Abort, [world, weak]()
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

	void ServerWorldTransitionCoordinator::Enter(UserId userId, const EnterWorldRequest& request, Timestamp nowNs)
	{
		if (!SubmitToUserShard(userId, Job(weak_from_this(), &ServerWorldTransitionCoordinator::EnterMembershipOnUserShard, eJobPriority::Control, userId, request, nowNs)))
		{
			JAM_LOG_WARN("Failed to route world enter request to user shard. userId={}", userId);
		}
	}

	void ServerWorldTransitionCoordinator::Leave(UserId userId, const LeaveWorldRequest& request)
	{
		if (!SubmitToUserShard(userId, Job(weak_from_this(), &ServerWorldTransitionCoordinator::LeaveMembershipOnUserShard, eJobPriority::Control, userId, request)))
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
		if (!user || !user->worldTransition.IsActive())
			return nullptr;

		const WorldTransitionState& transition = user->worldTransition;
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
		user.worldTransition = std::move(transition);
		return user.worldTransition;
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

	void ServerWorldTransitionCoordinator::CompleteEnter(UserContext& user, const UserWorldState& state)
	{
		const WorldTransitionState transition = user.worldTransition;
		user.worldTransition = {};

		SendToUser(transition.userId, codec::MakeWorldTransitionResultPacket(
			{
				.kind			 = eWorldTransitionKind::Enter,
				.requestId		 = transition.requestId,
				.transitionToken = transition.token,
				.state			 = state
			}), eProtocolType::TCP);

		SendToUser(transition.userId, codec::MakeUserMainWorldChangedPacket(state), eProtocolType::TCP);

		if (user.IsConnected())
			if (m_owner) m_owner->GetWorldSyncSystem()->BeginWorldSync(user);
	}

	void ServerWorldTransitionCoordinator::CompleteLeave(UserContext& user, const UserWorldState& state)
	{
		const WorldTransitionState transition = user.worldTransition;
		user.worldTransition = {};

		if (transition.reason == eWorldTransitionReason::Request)
		{
			SendToUser(transition.userId, codec::MakeWorldTransitionResultPacket(
				{
					.kind			 = eWorldTransitionKind::Leave,
					.requestId		 = transition.requestId,
					.transitionToken = transition.token,
					.state			 = state
				}), eProtocolType::TCP);

			SendToUser(transition.userId, codec::MakeUserMainWorldChangedPacket(state), eProtocolType::TCP);

		}

		if (user.IsConnected() && user.worldSync.phase == eUserWorldSyncPhase::WaitingTransition)
			if (m_owner) m_owner->GetWorldSyncSystem()->BeginWorldSync(user);

		if (user.CanRelease())
			GetOrCreateUserShardState(CurrentShardLocalChecked()).FreeUserContext(transition.userId);
	}




	void ServerWorldTransitionCoordinator::Tick(Timestamp now)
	{
		auto* local = CurrentShardLocal();
		if (!local) return;

		auto& state = GetOrCreateUserShardState(*local);
		for (auto& entry : state.usersById.entries)
		{
			if (!entry.occupied)
				continue;

			if (entry.value.worldTransition.IsActive())
			{
				const auto& transition = entry.value.worldTransition;
				if (transition.deadline <= now || (transition.retryAt != Timestamp::Zero() && transition.retryAt <= now))
				{
					HandleWorldTransitionEvent(TransitionTimedOutEvent{
							.continuation = { transition.userId, transition.token, transition.phase },
							.now = now,
						});
				}
			}


			if (entry.occupied
				&& entry.value.connectionState == eUserConnectionState::Reconnecting
				&& entry.value.reconnectDeadline <= now)
			{
				ReleaseMembershipUser(entry.value);
			}

			if (entry.occupied && entry.value.connectionState == eUserConnectionState::Released
				&& entry.value.worldState.main
				&& !entry.value.worldTransition.IsActive())
			{
				ReleaseMembershipUser(entry.value);
			}
		}
	}

	void ServerWorldTransitionCoordinator::OnDisconnected(uint64 userId)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user || user->connectionState == eUserConnectionState::Released)
			return;

		user->connectionState     = eUserConnectionState::Reconnecting;
		user->reconnectDeadline	  = NOW() + m_reconnectTimeout;
		ServerWorldSyncSystem::ResetWorldSync(*user);

		if (!m_owner)
		{
			ReleaseMembershipUser(*user);
			return;
		}

		if (user->worldTransition.IsActive())
		{
			const auto& transition = user->worldTransition;
			HandleWorldTransitionEvent(UserDisconnectedEvent{
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
				if (!user->HasBothSessions())
					return;

				user->connectionState	= eUserConnectionState::Connected;
				user->reconnectDeadline = Timestamp::Zero();

				RefreshWorldUserContext(*user);
				if (m_owner) m_owner->GetWorldSyncSystem()->BeginWorldSync(*user);

				return;
			}

			if (!user->IsConnected())
				return;

			RefreshWorldUserContext(*user);
			if (m_owner) m_owner->GetWorldSyncSystem()->TryCompleteWorldSync(*user);
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

	void ServerWorldTransitionCoordinator::EnterMembershipOnUserShard(UserId userId, const EnterWorldRequest& request, Timestamp now)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user)
		{
			JAM_LOG_WARN("World transition requested for missing user. userId={}", userId);
			return;
		}

		if (!m_owner || !request.IsValid() || user->HasPendingWorldWork())
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
		transition.deadline				= now + m_transitionTimeout;

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
								resumed->HandleWorldTransitionEvent(WorldReadyEvent{ continuation, world });
						}, eJobPriority::Control));
				}
			});
	}

	void ServerWorldTransitionCoordinator::LeaveMembershipOnUserShard(UserId userId, const LeaveWorldRequest& request)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user)
		{
			JAM_LOG_WARN("World transition requested for missing user. userId={}", userId);
			return;
		}

		const auto current = user->worldState;
		if (!m_owner || user->HasPendingWorldWork() || current.revision != request.expectedMainRevision)
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
			.deadline			= NOW() + m_transitionTimeout,
		};
		BeginTransition(*user, std::move(transition), eWorldTransitionPhase::DetachingMembership);

		if (!AdvanceMembershipTransition(*user, eWorldTransitionPhase::DetachingMembership))
			FailMembershipTransition(*user, eWorldTransitionFailure::WorldDestroyed);
	}


	bool ServerWorldTransitionCoordinator::SubmitMembershipCommand(
		const WorldRef& world,
		const WorldTransitionContinuation& continuation,
		std::function<eWorldTransitionFailure(WorldShardState&)> command)
	{
		const auto weak = weak_from_this();

		return SubmitToWorldShard(world.worldId, Job([continuation, command = std::move(command), weak]() mutable
			{
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				const auto failure = command(state);

				SubmitToUserShard(continuation.userId, Job([continuation, failure, weak]()
					{
						if (const auto coordinator = weak.lock())
						{
							coordinator->HandleWorldTransitionEvent(WorldCommandCompletedEvent{ continuation, failure });
						}
					}, eJobPriority::Control));
			}, eJobPriority::Control));
	}

	bool ServerWorldTransitionCoordinator::AdvanceMembershipTransition(UserContext& user, eWorldTransitionPhase phase)
	{
		auto& transition = user.worldTransition;
		transition.phase    = phase;
		transition.retryAt  = Timestamp::Zero();
		transition.inFlight = true;

		const WorldTransitionContinuation continuation{ user.userId, transition.token, phase };
		const auto token  = transition.token;
		const auto userId = user.userId;

		WorldUserContext member{
			.accountId		= user.accountId,
			.userId			= userId,
			.mainRevision	= user.worldState.revision,
			.sessions		= ResolveUserSessionBundle(user),
		};

		bool submitted = false;

		switch (phase)
		{
		case eWorldTransitionPhase::JoiningMembership:
		{
			member.mainRevision = transition.expectedRevision + 1;
			const auto target = *transition.target;
			submitted = SubmitMembershipCommand(target, continuation,
				[target, member, token, entryPoint = transition.contentEntryPoint](WorldShardState& state) mutable
				{
					return state.JoinWorldMembership(target, std::move(member), token, std::move(entryPoint));
				});
			break;
		}

		case eWorldTransitionPhase::DetachingMembership:
		case eWorldTransitionPhase::FinalizingMembershipLeave:
		case eWorldTransitionPhase::RecoveringMembership:
		{
			const auto source = *transition.source;
			const bool disconnect = transition.reason == eWorldTransitionReason::Disconnect;

			if (phase == eWorldTransitionPhase::RecoveringMembership && user.worldState.main != transition.source)
				++member.mainRevision; // Correlate restored Player with the following Main change.

			submitted = SubmitMembershipCommand(source, continuation, [source, token, userId, member, phase, disconnect](WorldShardState& state)
				{
					if (disconnect && !state.FindWorld(source.worldId))
						return eWorldTransitionFailure::None;

					bool succeeded = false;

					if (phase == eWorldTransitionPhase::DetachingMembership)
						succeeded = state.DetachWorldMembership(source, userId, token);
					else if (phase == eWorldTransitionPhase::RecoveringMembership)
						succeeded = state.RecoverWorldMembership(source, member, token);
					else
						succeeded = state.CompleteWorldMembershipLeave(source, userId, token);

					return succeeded ? eWorldTransitionFailure::None : eWorldTransitionFailure::InternalError;
				});
			break;
		}

		case eWorldTransitionPhase::CancellingMembership:
		case eWorldTransitionPhase::AcceptingMembership:
		{
			const auto target = *transition.target;
			submitted = SubmitMembershipCommand(target, continuation, [target, token, userId, phase](WorldShardState& state)
				{
					const bool succeeded = phase == eWorldTransitionPhase::CancellingMembership
						? state.CancelWorldMembership(target, userId, token)
						: state.CompleteWorldMembershipEntry(target, userId, token);

					return succeeded ? eWorldTransitionFailure::None : eWorldTransitionFailure::InternalError;
				});
			break;
		}

		default:
			break;
		}

		if (!submitted)
		{
			transition.inFlight = false;
			transition.retryAt  = NOW() + 1_s;
		}

		return submitted;
	}

	void ServerWorldTransitionCoordinator::FailMembershipTransition(UserContext& user, eWorldTransitionFailure failure)
	{
		auto& transition = user.worldTransition;

		if (transition.reason == eWorldTransitionReason::Disconnect)
		{
			if (!transition.inFlight)
				transition.retryAt = NOW() + 1_s;

			return;
		}

		if (transition.IsCommitted())
			return;

		if (transition.failure == eWorldTransitionFailure::None)
			transition.failure = failure;

		if (transition.inFlight)
			return;

		if (transition.phase == eWorldTransitionPhase::CancellingMembership || transition.phase == eWorldTransitionPhase::RecoveringMembership)
			return;

		if (transition.target
			&& transition.phase == eWorldTransitionPhase::JoiningMembership
			&& transition.kind == eWorldTransitionKind::Enter)
		{
			AdvanceMembershipTransition(user, eWorldTransitionPhase::CancellingMembership);
			return;
		}

		FinishMembershipFailure(user);
	}


	void ServerWorldTransitionCoordinator::HandleWorldTransitionEvent(const WorldTransitionEvent& event)
	{
		if (const auto* ready = std::get_if<WorldReadyEvent>(&event))
		{
			auto* user = FindExpectedTransition(ready->continuation);
			if (!user) return;

			auto& transition = user->worldTransition;
			if (!ready->world || ready->world->instance != transition.targetInstance)
			{
				FailMembershipTransition(*user, eWorldTransitionFailure::DestinationUnavailable);
				return;
			}

			if (transition.source == ready->world)
			{
				CompleteMembershipTransition(*user);
				return;
			}

			transition.target = ready->world;
			if (!AdvanceMembershipTransition(*user, transition.source ? eWorldTransitionPhase::DetachingMembership : eWorldTransitionPhase::JoiningMembership))
				FailMembershipTransition(*user, eWorldTransitionFailure::WorldDestroyed);

			return;
		}

		if (const auto* timedOut = std::get_if<TransitionTimedOutEvent>(&event))
		{
			auto* user = FindExpectedTransition(timedOut->continuation);
			if (!user) return;

			auto& transition = user->worldTransition;
			if (transition.reason == eWorldTransitionReason::Disconnect)
			{
				if (!transition.inFlight && transition.retryAt <= timedOut->now)
					AdvanceMembershipTransition(*user, transition.phase);

				return;
			}

			if (transition.inFlight)
			{
				FailMembershipTransition(*user, eWorldTransitionFailure::Timeout);
				return;
			}

			if (transition.IsCleanupPending())
			{
				if (transition.retryAt <= timedOut->now)
					AdvanceMembershipTransition(*user, transition.phase);
			}
			else
			{
				FailMembershipTransition(*user, eWorldTransitionFailure::Timeout);
			}

			return;
		}

		if (const auto* disconnected = std::get_if<UserDisconnectedEvent>(&event))
		{
			if (auto* user = FindExpectedTransition(disconnected->continuation))
				FailMembershipTransition(*user, eWorldTransitionFailure::Disconnected);
			return;
		}

		const auto& completed = std::get<WorldCommandCompletedEvent>(event);
		auto* user = FindExpectedTransition(completed.continuation);
		if (!user) return;

		auto& transition = user->worldTransition;
		transition.inFlight = false;

		const auto phase	 = completed.continuation.expectedPhase;
		const bool succeeded = completed.failure == eWorldTransitionFailure::None;

		if (phase == eWorldTransitionPhase::DetachingMembership)
		{
			if (!succeeded)
			{
				FailMembershipTransition(*user, completed.failure);
				return;
			}

			if (transition.failure != eWorldTransitionFailure::None)
			{
				AdvanceMembershipTransition(*user, eWorldTransitionPhase::RecoveringMembership);
				return;
			}

			if (transition.kind == eWorldTransitionKind::Leave)
			{
				if (!CommitMain(user->userId, transition.expectedRevision, std::nullopt))
				{
					transition.failure = eWorldTransitionFailure::InternalError;
					AdvanceMembershipTransition(*user, eWorldTransitionPhase::RecoveringMembership);
					return;
				}

				AdvanceMembershipTransition(*user, eWorldTransitionPhase::FinalizingMembershipLeave);
			}
			else if (!AdvanceMembershipTransition(*user, eWorldTransitionPhase::JoiningMembership))
			{
				FailMembershipTransition(*user, eWorldTransitionFailure::InternalError);
			}

			return;
		}

		if (transition.IsCleanupPending())
		{
			if (!succeeded)
			{
				transition.retryAt = NOW() + 1_s;
				JAM_LOG_WARN("Membership cleanup/recovery will retry. userId={}, token={}, phase={}", user->userId, transition.token.value, static_cast<uint32>(phase));
				return;
			}

			if (phase == eWorldTransitionPhase::CancellingMembership)
			{
				if (transition.source)
				{
					AdvanceMembershipTransition(*user, eWorldTransitionPhase::RecoveringMembership);
				}
				else
				{
					if (user->worldState.main == transition.target)
						CommitMain(user->userId, user->worldState.revision, std::nullopt);

					FinishMembershipFailure(*user);
				}
			}
			else if (phase == eWorldTransitionPhase::RecoveringMembership)
			{
				if (user->worldState.main != transition.source)
					CommitMain(user->userId, user->worldState.revision, transition.source);

				FinishMembershipFailure(*user);
			}
			else if (phase == eWorldTransitionPhase::AcceptingMembership)
			{
				if (transition.source)
					AdvanceMembershipTransition(*user, eWorldTransitionPhase::FinalizingMembershipLeave);
				else
					CompleteMembershipTransition(*user);
			}
			else
			{
				CompleteMembershipTransition(*user);
			}

			return;
		}

		if (transition.failure != eWorldTransitionFailure::None || !succeeded)
		{
			FailMembershipTransition(*user, transition.failure != eWorldTransitionFailure::None ? transition.failure : completed.failure);
			return;
		}

		if (!CommitMain(user->userId, transition.expectedRevision, transition.target))
		{
			FailMembershipTransition(*user, eWorldTransitionFailure::InternalError);
			return;
		}

		RefreshWorldUserContext(*user);
		AdvanceMembershipTransition(*user, eWorldTransitionPhase::AcceptingMembership);
	}

	void ServerWorldTransitionCoordinator::FinishMembershipFailure(UserContext& user)
	{
		const bool syncRequired = user.worldState.main.has_value() || user.worldSync.phase == eUserWorldSyncPhase::WaitingTransition;

		const WorldTransitionState transition = user.worldTransition;
		const UserWorldState state = user.worldState;
		user.worldTransition = {};

		SendToUser(transition.userId, codec::MakeWorldTransitionResultPacket(
			{
				.kind			 = transition.kind,
				.requestId		 = transition.requestId,
				.transitionToken = transition.token,
				.failure		 = transition.failure,
				.state			 = state
			}), eProtocolType::TCP);

		if (transition.kind == eWorldTransitionKind::Enter)
			JAM_LOG_WARN("[EnterWorld] failed. userId={}, requestId={}, token={}, failure={}", transition.userId, transition.requestId, transition.token.value, static_cast<uint32>(transition.failure));

		if (m_owner && syncRequired && user.IsConnected())
		{
			m_owner->GetWorldSyncSystem()->BeginWorldSync(user);
		}

		if (user.CanRelease())
		{
			GetOrCreateUserShardState(CurrentShardLocalChecked()).FreeUserContext(transition.userId);
			return;
		}

		if (user.connectionState == eUserConnectionState::Released)
		{
			ReleaseMembershipUser(user);
		}
	}

	void ServerWorldTransitionCoordinator::CompleteMembershipTransition(UserContext& user)
	{
		const UserId userId = user.userId;

		if (user.worldTransition.kind == eWorldTransitionKind::Enter)
			CompleteEnter(user, user.worldState);
		else
			CompleteLeave(user, user.worldState);

		if (auto* remaining = FindUserOnCurrentShard(userId); remaining && remaining->connectionState == eUserConnectionState::Released)
			ReleaseMembershipUser(*remaining);
	}

	void ServerWorldTransitionCoordinator::ReleaseMembershipUser(UserContext& user)
	{
		if (user.connectionState != eUserConnectionState::Reconnecting && user.connectionState != eUserConnectionState::Released)
			return;

		const UserId userId = user.userId;

		if (user.connectionState != eUserConnectionState::Released)
		{
			user.connectionState   = eUserConnectionState::Released;
			user.reconnectDeadline = Timestamp::Zero();
			GetOrCreateUserShardState(CurrentShardLocalChecked()).ReleaseAccountBinding(userId);
			if (m_owner)
				m_owner->NotifyUserReleased(userId);
		}

		if (user.worldTransition.IsActive())
		{
			const auto& transition = user.worldTransition;
			HandleWorldTransitionEvent(UserDisconnectedEvent{ { userId, transition.token, transition.phase } });
			return; // Recovery/commit must finish before disconnecting the resulting membership.
		}

		if (!user.worldState.main)
		{
			if (user.HasNoSessions())
				GetOrCreateUserShardState(CurrentShardLocalChecked()).FreeUserContext(userId);
			return;
		}

		WorldTransitionState transition{};
		transition.token			= IssueTransitionToken();
		transition.kind				= eWorldTransitionKind::Leave;
		transition.userId			= userId;
		transition.source			= user.worldState.main;
		transition.expectedRevision = user.worldState.revision;
		transition.reason			= eWorldTransitionReason::Disconnect;
		transition.deadline			= NOW() + m_transitionTimeout;

		BeginTransition(user, std::move(transition), eWorldTransitionPhase::DetachingMembership);
		AdvanceMembershipTransition(user, eWorldTransitionPhase::DetachingMembership);
	}
}
