#include "pch.h"
#include "jamnet/runtime/world/lifecycle/ServerWorldTransitionCoordinator.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/utils/Clock.h"
#include "jamnet/runtime/application/ServerNetworkManager.h"
#include "jamnet/runtime/protocol/transport/WireBarrier.h"
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

		const auto* manifest = owner->GetSharedDataManifest();
		m_templatesDB    = WorldTemplatesLoader::Load(manifest->worldTemplateDatabasePath);
		m_archetypesDB   = WorldArchetypesLoader::Load(manifest->worldArchetypeDatabasePath);
		m_instancesDB    = WorldInstancesLoader::Load(manifest->worldInstanceDatabasePath);
		m_actorArchetypesDB = ActorArchetypesLoader::Load(manifest->actorArchetypeDatabasePath);
		m_configResolver = std::make_unique<WorldConfigResolver>(manifest, &m_templatesDB, &m_archetypesDB);

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

			shard->Submit(Job([directory = &m_directory, shardDone]()
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
							[directory, worldId, remaining, shardDone]()
								{
									directory->Clear(worldId);
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

		std::vector<RuntimeWaiter> runtimeWaiters;
		{
			std::scoped_lock lock(m_runtimeCreationMutex);
			for (auto& waiters : m_runtimeWaiters | std::views::values)
				for (auto& waiter : waiters)
					runtimeWaiters.push_back(std::move(waiter));
			m_runtimeWaiters.clear();
		}
		for (auto& waiter : runtimeWaiters)
		{
			if (!waiter.completed)
				continue;
			waiter.completed(std::nullopt);
		}
		m_sendPrepare = {};
		m_sendCommit = {};
		m_transitionResult = {};
		m_mainChanged = {};
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
			EnsureRuntimeAsync(instance, [state](std::optional<WorldRuntimeRef> runtime)
				{
					if (!runtime)
						state->succeeded.store(false, std::memory_order_relaxed);
					if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1 && state->completed)
						state->completed(state->succeeded.load(std::memory_order_relaxed));
				});
		}
	}

	void ServerWorldTransitionCoordinator::DestroyRuntime(const WorldRuntimeRef& runtime)
	{
		if (!runtime.IsValid())
			return;
		if (auto record = m_directory.FindByInstanceId(runtime.instance.instanceId); record && record->runtime == runtime)
		{
			record->state = eWorldRuntimeState::Destroying;
			m_directory.Publish(*record);
		}
		auto shard = GLOBAL_EXEC.GetShardFromIndex(GetWorldShardIndex(runtime.worldId));
		if (!shard) return;

		auto* directory = &m_directory;
		shard->Submit(Job([runtime, directory]()
			{
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				if (const auto* entry = state.FindAuthoritativeWorldEntry(runtime.worldId))
				{
					const eWorldRuntimeState previous = entry->state;
					if (!state.BeginDestroyWorld(runtime.worldId, eMailboxCloseMode::Abort, [directory, worldId = runtime.worldId]()
						{
							directory->Clear(worldId);
						}))
					{
						if (auto record = directory->FindByInstanceId(runtime.instance.instanceId); record && record->runtime == runtime)
						{
							record->state = previous;
							directory->Publish(*record);
						}
					}
				}
				else
				{
					directory->Clear(runtime.worldId);
				}
			}, eJobPriority::Control));
	}

	bool ServerWorldTransitionCoordinator::SubmitWorldJob(const WorldRuntimeRef& runtime, std::function<void(WorldBase&)> job)
	{
		if (!runtime.IsValid() || !job)
			return false;
		const uint16 shardIndex = GetWorldShardIndex(runtime.worldId);
		auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
		if (!shard)
			return false;
		auto invoke = [runtimeId = runtime.worldId, job = std::move(job)](ShardLocal& local) mutable
			{
				if (auto* world = GetOrCreateWorldShardState(local).FindWorld(runtimeId))
					job(*world);
			};
		if (auto* local = CurrentShardLocal(); local && local->shardIndex == shardIndex)
			invoke(*local);
		else
			shard->Submit(Job([invoke = std::move(invoke)]() mutable { invoke(CurrentShardLocalChecked()); }, eJobPriority::Normal));
		return true;
	}

	void ServerWorldTransitionCoordinator::SetTransport(SendPrepareFn prepare, SendCommitFn commit, TransitionResultFn result, MainChangedFn changed)
	{
		m_sendPrepare = std::move(prepare);
		m_sendCommit = std::move(commit);
		m_transitionResult = std::move(result);
		m_mainChanged = std::move(changed);
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

	bool ServerWorldTransitionCoordinator::SubmitToUserShard(uint64 userId, std::function<void()> job)
	{
		if (!job)
			return false;

		const uint16 shardIndex = GetUserShardIndex(userId);
		if (auto* local = CurrentShardLocal(); local && local->shardIndex == shardIndex)
		{
			job();
			return true;
		}

		auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
		if (!shard)
			return false;
		shard->Submit(Job([job = std::move(job)]() mutable { job(); }, eJobPriority::Control));
		return true;
	}

	std::optional<UserPhysicalWorldState> ServerWorldTransitionCoordinator::CommitMain(uint64 userId, WorldStateRevision expectedRevision, std::optional<WorldRuntimeRef> main)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user || user->physicalWorld.revision != expectedRevision)
			return std::nullopt;

		if (main)
		{
			if (!user->physicalWorld.SetMain(*main) && (!user->physicalWorld.main || *user->physicalWorld.main != *main))
				return std::nullopt;
		}
		else if (user->physicalWorld.main)
		{
			user->physicalWorld.ClearIfRuntime(user->physicalWorld.main->worldId);
		}

		return user->physicalWorld;
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

	void ServerWorldTransitionCoordinator::SendResult(uint64 userId, const WorldTransitionResult& result) const
	{
		if (m_transitionResult)
			m_transitionResult(userId, result);
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

	void ServerWorldTransitionCoordinator::EnsureRuntimeAsync(const WorldInstanceRef& instance, RuntimeReadyFn completed)
	{
		if (!completed)
			return;
		if (const auto record = m_directory.FindByInstanceId(instance.instanceId);
			record && record->instance == instance && record->runtime.IsValid())
		{
			completed(record->runtime);
			return;
		}

		bool startCreation = false;
		std::optional<WorldRuntimeRef> readyRuntime;
		{
			std::scoped_lock lock(m_runtimeCreationMutex);
			if (const auto record = m_directory.FindByInstanceId(instance.instanceId);
				record && record->instance == instance && record->runtime.IsValid())
			{
				readyRuntime = record->runtime;
			}
			else
			{
				auto& waiters = m_runtimeWaiters[instance.instanceId.value];
				startCreation = waiters.empty();
				waiters.push_back({ .completed = std::move(completed) });
			}
		}
		if (readyRuntime)
		{
			completed(*readyRuntime);
			return;
		}
		if (!startCreation)
			return;

		const auto* definition = m_instancesDB.Find(instance.instanceId);
		if (!m_owner || !m_configResolver || !definition || definition->Ref() != instance)
		{
			CompleteRuntimeCreation(instance, std::nullopt);
			return;
		}

		WorldConfig config = m_configResolver->ResolveWorldConfig(instance);
		if (!config.IsValid())
		{
			CompleteRuntimeCreation(instance, std::nullopt);
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
			CompleteRuntimeCreation(instance, std::nullopt);
			return;
		}

		const std::weak_ptr<ServerWorldTransitionCoordinator> weak = weak_from_this();

		shard->Submit(Job([instance, route, config = std::move(config), weak]() mutable
			{
				if (const auto coordinator = weak.lock())
				{
					if (!coordinator->m_owner)
					{
						coordinator->CompleteRuntimeCreation(instance, std::nullopt);
						return;
					}

						auto record = CreateWorldOnShard(
						GetOrCreateWorldShardState(CurrentShardLocalChecked()),
						route,
							config,
							coordinator->m_actorArchetypesDB,
							*coordinator->m_owner,
						weak);
					coordinator->CompleteRuntimeCreation(instance, record);
				}
			}, eJobPriority::Control));
	}

	void ServerWorldTransitionCoordinator::CompleteRuntimeCreation(const WorldInstanceRef& instance, std::optional<WorldRecord> record)
	{
		if (record)
		{
			if (const auto* definition = m_instancesDB.Find(instance.instanceId))
			{
				record->startup = definition->startup;
				record->lifecycle = definition->lifecycle;
			}
			if (!m_directory.Publish(*record))
				record.reset();
		}

		std::vector<RuntimeWaiter> waiters;
		{
			std::scoped_lock lock(m_runtimeCreationMutex);
			if (auto it = m_runtimeWaiters.find(instance.instanceId.value); it != m_runtimeWaiters.end())
			{
				waiters = std::move(it->second);
				m_runtimeWaiters.erase(it);
			}
		}
		const std::optional<WorldRuntimeRef> runtime = record ? std::optional(record->runtime) : std::nullopt;
		for (auto& waiter : waiters)
		{
			if (waiter.completed)
				waiter.completed(runtime);
		}
	}

	bool ServerWorldTransitionCoordinator::SubmitWorldCommand(const WorldRuntimeRef& runtime,
		const WorldTransitionContinuation& continuation, std::function<bool(WorldShardState&)> command)
	{
		if (!runtime.IsValid() || !command)
			return false;
		auto shard = GLOBAL_EXEC.GetShardFromIndex(GetWorldShardIndex(runtime.worldId));
		if (!shard)
			return false;

		const std::weak_ptr<ServerWorldTransitionCoordinator> weak = weak_from_this();
		shard->Submit(Job([runtimeId = runtime.worldId, continuation, command = std::move(command), weak]() mutable
			{
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				const bool succeeded = state.FindAuthoritativeWorldEntry(runtimeId) && command(state);
				auto userShard = GLOBAL_EXEC.GetShardFromIndex(GetUserShardIndex(continuation.userId));
				if (!userShard)
					return;
				userShard->Submit(Job([continuation, succeeded, weak]()
					{
						if (const auto coordinator = weak.lock())
							coordinator->OnWorldCommandCompleted(continuation, succeeded);
					}, eJobPriority::Control));
			}, eJobPriority::Control));
		return true;
	}

	bool ServerWorldTransitionCoordinator::SubmitReserveTarget(UserContext& user)
	{
		auto& transition = *user.worldTransition.active;
		transition.phase = eWorldTransitionPhase::ReservingTarget;

		const auto continuation = WorldTransitionContinuation{ transition.userId, transition.token, transition.phase };
		const auto token		= transition.token;
		const auto userId		= transition.userId;
		const auto target		= *transition.target;
		
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

	bool ServerWorldTransitionCoordinator::SubmitDetachSource(UserContext& user)
	{
		auto& transition = *user.worldTransition.active;
		transition.phase = eWorldTransitionPhase::DetachingSource;

		const auto continuation = WorldTransitionContinuation{ transition.userId, transition.token, transition.phase };
		const auto token		= transition.token;
		const auto userId		= transition.userId;
		const auto source		= *transition.source;

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

	bool ServerWorldTransitionCoordinator::SubmitAttachTarget(UserContext& user)
	{
		auto& transition = *user.worldTransition.active;
		transition.phase = eWorldTransitionPhase::AttachingTarget;
		const WorldUserContext worldUser
		{
			.accountId	  = user.accountId,
			.userId		  = transition.userId,
			.mainRevision = transition.expectedRevision + 1,
			.sessions	  = m_owner ? m_owner->GetSessionBundle(transition.userId) : ServerSessionBundle{},
		};
		const auto continuation = WorldTransitionContinuation{ transition.userId, transition.token, transition.phase };
		const auto token		= transition.token;

		return SubmitWorldCommand(*transition.target, continuation, [token, worldUser](WorldShardState& state)
			{ return state.AttachPrepared(token, worldUser); });
	}

	bool ServerWorldTransitionCoordinator::SubmitPrepareTargetContent(UserContext& user)
	{
		auto& transition = *user.worldTransition.active;
		transition.phase = eWorldTransitionPhase::PreparingTargetContent;

		const WorldTransitionContinuation continuation{
			transition.userId,
			transition.token,
			transition.phase,
		};
		const ServerWorldMemberContentContext context{
			.accountId = user.accountId,
			.userId = transition.userId,
			.transitionToken = transition.token,
			.correlation = {
				.world = *transition.target,
				.mainRevision = transition.expectedRevision + 1,
			},
			.entryPoint = transition.contentEntryPoint,
		};
		const WorldRuntimeRef target = *transition.target;
		auto shard = GLOBAL_EXEC.GetShardFromIndex(GetWorldShardIndex(target.worldId));
		if (!shard)
			return false;

		const std::weak_ptr<ServerWorldTransitionCoordinator> weak = weak_from_this();
		shard->Submit(Job([target, continuation, context, weak]() mutable
			{
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				auto* world = dynamic_cast<ServerWorld*>(state.FindWorld(target.worldId));
				if (!world)
				return;

				world->PrepareMemberContent(context, [continuation, weak](bool succeeded)
					{
						auto userShard = GLOBAL_EXEC.GetShardFromIndex(GetUserShardIndex(continuation.userId));
						if (!userShard)
							return;
						userShard->Submit(Job([continuation, succeeded, weak]()
							{
								if (const auto coordinator = weak.lock())
									coordinator->OnWorldCommandCompleted(continuation, succeeded);
							}, eJobPriority::Control));
					});
			}, eJobPriority::Control));
		return true;
	}

	bool ServerWorldTransitionCoordinator::SubmitActivateTarget(UserContext& user)
	{
		auto& transition = *user.worldTransition.active;
		transition.phase = eWorldTransitionPhase::ActivatingTarget;

		const auto continuation = WorldTransitionContinuation{ transition.userId, transition.token, transition.phase };
		const auto token		= transition.token;

		return SubmitWorldCommand(*transition.target, continuation, [token](WorldShardState& state)
			{ return state.ActivateAttached(token); });
	}

	bool ServerWorldTransitionCoordinator::SubmitCommitSourceLeave(UserContext& user)
	{
		auto& transition = *user.worldTransition.active;
		transition.phase = eWorldTransitionPhase::CommittingSourceLeave;

		const auto continuation = WorldTransitionContinuation{ transition.userId, transition.token, transition.phase };
		const auto token = transition.token;

		return SubmitWorldCommand(*transition.source, continuation, [token](WorldShardState& state)
			{ return state.CommitLeave(token); });
	}

	bool ServerWorldTransitionCoordinator::SubmitRollbackTarget(UserContext& user)
	{
		auto& transition = *user.worldTransition.active;
		transition.phase = eWorldTransitionPhase::RollingBackTarget;

		const auto continuation = WorldTransitionContinuation{ transition.userId, transition.token, transition.phase };
		const auto token		= transition.token;
		
		return SubmitWorldCommand(*transition.target, continuation, [token](WorldShardState& state)
			{ return state.RollbackEnter(token); });
	}

	bool ServerWorldTransitionCoordinator::SubmitRestoreSource(UserContext& user)
	{
		auto& transition = *user.worldTransition.active;
		transition.phase = eWorldTransitionPhase::RestoringSource;
		const WorldUserContext worldUser
		{
			.accountId	  = user.accountId,
			.userId		  = transition.userId,
			.mainRevision = user.physicalWorld.revision,
			.sessions	  = m_owner ? m_owner->GetSessionBundle(transition.userId) : ServerSessionBundle{},
		};

		const auto continuation = WorldTransitionContinuation{ transition.userId, transition.token, transition.phase };
		const auto token		= transition.token;
		
		return SubmitWorldCommand(*transition.source, continuation, [token, worldUser](WorldShardState& state)
			{ return state.RestoreDetachedMain(token, worldUser); });
	}

	void ServerWorldTransitionCoordinator::Enter(uint64 userId, const EnterWorldRequest& request, uint64 nowNs)
	{
		const std::weak_ptr<ServerWorldTransitionCoordinator> weak = weak_from_this();
		if (!SubmitToUserShard(userId, [weak, userId, request, nowNs]()
			{
				if (const auto coordinator = weak.lock())
					coordinator->EnterOnUserShard(userId, request, nowNs);
			}))
		{
			SendResult(userId, 
				{ 
					.kind	   = eWorldTransitionKind::Enter,
					.requestId = request.requestId, 
					.failure   = eWorldTransitionFailure::InvalidRequest 
				});
		}
	}

	void ServerWorldTransitionCoordinator::EnterOnUserShard(uint64 userId, const EnterWorldRequest& request, uint64 nowNs)
	{
		JAM_ASSERT(CurrentShardLocal() && CurrentShardLocal()->shardIndex == GetUserShardIndex(userId));
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!m_owner || !user || !request.IsValid() || user->worldTransition.active)
		{
			SendResult(userId, 
				{ 
					.kind		= eWorldTransitionKind::Enter, 
					.requestId	= request.requestId, 
					.failure	= eWorldTransitionFailure::InvalidRequest 
				});
			return;
		}

		const UserPhysicalWorldState current = user->physicalWorld;
		if (current.revision != request.expectedMainRevision)
		{
			SendResult(userId, 
				{ 
					.kind		= eWorldTransitionKind::Enter, 
					.requestId	= request.requestId, 
					.failure	= eWorldTransitionFailure::StaleRevision, 
					.state		= current 
				});
			return;
		}

		const auto instance = ResolveDestination(request);
		if (!instance)
		{
			SendResult(userId, 
				{ 
					.kind		= eWorldTransitionKind::Enter, 
					.requestId	= request.requestId, 
					.failure	= eWorldTransitionFailure::DestinationUnavailable, 
					.state		= current 
				});
			return;
		}

		WorldTransitionState transition = {};
		transition.token			= IssueTransitionToken(); 
		transition.kind				= eWorldTransitionKind::Enter;
		transition.userId			= userId;
		transition.requestId		= request.requestId;
		transition.phase			= eWorldTransitionPhase::ResolvingTarget; 
		transition.source			= current.main; 
		transition.targetInstance	= *instance;
		transition.expectedRevision	= current.revision; 
		transition.contentEntryPoint	= request.contentEntryPoint;
		transition.barrierToken		= WireBarrierToken{ transition.token.value };
		transition.deadlineNs		= nowNs + m_barrierTimeoutNs;

		user->worldTransition.active = transition;
		JAMNET_LOG_INFO("[EnterWorld] resolved. userId={}, requestId={}, token={}, instance={}",
			userId, request.requestId, transition.token.value, instance->instanceId.value);

		const WorldTransitionContinuation continuation{ userId, transition.token, eWorldTransitionPhase::ResolvingTarget };
		const std::weak_ptr<ServerWorldTransitionCoordinator> weak = weak_from_this();
		EnsureRuntimeAsync(*instance, [weak, continuation](std::optional<WorldRuntimeRef> runtime)
			{
				if (const auto coordinator = weak.lock())
				{
					coordinator->SubmitToUserShard(continuation.userId, [weak, continuation, runtime]()
						{
							if (const auto resumed = weak.lock())
								resumed->OnRuntimeReady(continuation, runtime);
						});
				}
			});
	}

	void ServerWorldTransitionCoordinator::OnRuntimeReady(const WorldTransitionContinuation& continuation, std::optional<WorldRuntimeRef> runtime)
	{
		JAM_ASSERT(CurrentShardLocal() && CurrentShardLocal()->shardIndex == GetUserShardIndex(continuation.userId));
		UserContext* user = FindExpectedTransition(continuation);
		if (!user) return;

		auto& transition = *user->worldTransition.active;
		if (!runtime || runtime->instance != transition.targetInstance)
		{
			transition.terminalFailure = eWorldTransitionFailure::DestinationUnavailable;
			FinishFailure(*user);
			return;
		}
		JAMNET_LOG_INFO("[EnterWorld] runtime ready. userId={}, token={}, worldId={}",
			transition.userId, transition.token.value, runtime->worldId);
		if (transition.source && *transition.source == *runtime)
		{
			CompleteEnter(*user, user->physicalWorld);
			return;
		}
		transition.target = *runtime;
		if (!SubmitReserveTarget(*user))
			BeginFailure(*user, eWorldTransitionFailure::DestinationUnavailable);
	}

	void ServerWorldTransitionCoordinator::Leave(uint64 userId, const LeaveWorldRequest& request)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		const auto current = user ? user->physicalWorld : UserPhysicalWorldState{};
		if (!m_owner || !user || user->worldTransition.active || current.revision != request.expectedMainRevision)
		{
			SendResult(userId, 
				{ 
					.kind		= eWorldTransitionKind::Leave, 
					.requestId	= request.requestId, 
					.failure	= current.revision != request.expectedMainRevision ? eWorldTransitionFailure::StaleRevision : eWorldTransitionFailure::InvalidRequest, 
					.state		= current 
				});
			return;
		}
		if (!current.main)
		{
			SendResult(userId, 
				{ 
					.kind		= eWorldTransitionKind::Leave, 
					.requestId	= request.requestId, 
					.state		= current 
				});
			return;
		}

		WorldTransitionState transition
		{
			.token				= IssueTransitionToken(), 
			.kind				= eWorldTransitionKind::Leave, 
			.userId				= userId, 
			.requestId			= request.requestId,
			.phase				= eWorldTransitionPhase::DetachingSource, 
			.source				= current.main,
			.expectedRevision	= current.revision, 
			.deadlineNs			= NOW_NS() + m_barrierTimeoutNs,
		};
		user->worldTransition.active = transition;

		if (!SubmitDetachSource(*user))
		{
			user->worldTransition.active.reset();
			SendResult(userId, 
				{ 
					.kind			 = eWorldTransitionKind::Leave, 
					.requestId		 = request.requestId,
					.transitionToken = transition.token, 
					.failure		 = eWorldTransitionFailure::RuntimeDestroyed, 
					.state			 = current 
				});
		}
	}

	void ServerWorldTransitionCoordinator::OnClientBarrierResult(uint64 userId, const ClientBarrierResult& result, uint64 nowNs)
	{
		JAMNET_LOG_INFO("[EnterWorld] barrier result. userId={}, token={}, succeeded={}, failure={}",
			userId, result.token.value, result.succeeded, static_cast<uint32>(result.failure));

		const WorldTransitionToken token{ result.token.value };
		UserContext* user = FindExpectedTransition({ .userId = userId, .token = token, .expectedPhase = eWorldTransitionPhase::WaitingClientApplied });
		if (!user)
			user = FindExpectedTransition({ .userId = userId, .token = token, .expectedPhase = eWorldTransitionPhase::WaitingClientPrepared });
		if (!user)
			return;
		WorldTransitionState& transition = *user->worldTransition.active;
		if (transition.barrierToken != result.token)
			return;
		if (transition.deadlineNs <= nowNs)
		{
			BeginFailure(*user, eWorldTransitionFailure::Timeout);
			return;
		}
		if (transition.phase == eWorldTransitionPhase::WaitingClientApplied)
		{
			if (result.succeeded)
			{
				if (!SubmitActivateTarget(*user))
					BeginFailure(*user, eWorldTransitionFailure::InternalError);
			}
			else
				BeginFailure(*user, result.failure == eWorldTransitionFailure::None ? eWorldTransitionFailure::ClientPrepareFailed : result.failure);
			return;
		}
		if (transition.phase != eWorldTransitionPhase::WaitingClientPrepared)
			return;
		if (!result.succeeded)
		{
			BeginFailure(*user, result.failure == eWorldTransitionFailure::None ? eWorldTransitionFailure::ClientPrepareFailed : result.failure);
			return;
		}
		if (transition.source)
		{
			if (!SubmitDetachSource(*user))
				BeginFailure(*user, eWorldTransitionFailure::RuntimeDestroyed);
		}
		else if (!SubmitAttachTarget(*user))
		{
			BeginFailure(*user, eWorldTransitionFailure::InternalError);
		}
	}

	void ServerWorldTransitionCoordinator::OnWorldCommandCompleted(const WorldTransitionContinuation& continuation, bool succeeded)
	{
		UserContext* user = FindExpectedTransition(continuation);
		if (!user)
			return;
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
				transition.phase = eWorldTransitionPhase::WaitingClientPrepared;
				BeginFailure(*user, failure);
				return;
			}
		}
		switch (continuation.expectedPhase)
		{
		case eWorldTransitionPhase::ReservingTarget:
			if (!succeeded)
			{
				transition.terminalFailure = eWorldTransitionFailure::CapacityExceeded;
				FinishFailure(*user);
			}
			else
			{
				JAMNET_LOG_INFO("[EnterWorld] target reserved. userId={}, token={}; sending prepare",
					transition.userId, transition.token.value);
				SendClientPrepare(*user);
			}
			return;

		case eWorldTransitionPhase::DetachingSource:
			if (!succeeded)
			{
				BeginFailure(*user, eWorldTransitionFailure::RuntimeDestroyed);
				return;
			}
			transition.sourceDetached = true;
			if (transition.kind == eWorldTransitionKind::Leave)
			{
				transition.phase = eWorldTransitionPhase::CommittingMain;
				if (!CommitMain(transition.userId, transition.expectedRevision, std::nullopt) || !SubmitCommitSourceLeave(*user))
					BeginFailure(*user, eWorldTransitionFailure::InternalError);
			}
			else if (!SubmitAttachTarget(*user))
			{
				BeginFailure(*user, eWorldTransitionFailure::InternalError);
			}
			return;

		case eWorldTransitionPhase::AttachingTarget:
			if (!succeeded)
			{
				BeginFailure(*user, eWorldTransitionFailure::InternalError);
				return;
			}
			if (!SubmitPrepareTargetContent(*user))
				BeginFailure(*user, eWorldTransitionFailure::InternalError);
			return;

		case eWorldTransitionPhase::PreparingTargetContent:
			if (!succeeded)
			{
				BeginFailure(*user, eWorldTransitionFailure::InternalError);
				return;
			}
			transition.phase = eWorldTransitionPhase::CommittingMain;
			if (!CommitMain(transition.userId, transition.expectedRevision, transition.target))
				BeginFailure(*user, eWorldTransitionFailure::InternalError);
			else
				SendClientCommit(*user);
			return;

		case eWorldTransitionPhase::ActivatingTarget:
			if (!succeeded)
			{
				BeginFailure(*user, eWorldTransitionFailure::InternalError);
				return;
			}
			if (transition.source)
			{
				if (!SubmitCommitSourceLeave(*user))
					BeginFailure(*user, eWorldTransitionFailure::InternalError);
			}
			else
			{
				CompleteEnter(*user, user->physicalWorld);
			}
			return;

		case eWorldTransitionPhase::CommittingSourceLeave:
			if (transition.kind == eWorldTransitionKind::Enter)
			{
				if (!succeeded)
					JAMNET_LOG_WARN("Source leave commit failed after target activation. userId={}, token={}", transition.userId, transition.token.value);
				CompleteEnter(*user, user->physicalWorld);
			}
			else if (succeeded)
			{
				CompleteLeave(*user, user->physicalWorld);
			}
			else
			{
				BeginFailure(*user, eWorldTransitionFailure::InternalError);
			}
			return;

		case eWorldTransitionPhase::RollingBackTarget:
			if (transition.sourceDetached && transition.source)
			{
				if (!SubmitRestoreSource(*user))
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

	void ServerWorldTransitionCoordinator::SendClientPrepare(UserContext& user)
	{
		auto& transition = *user.worldTransition.active;
		transition.phase = eWorldTransitionPhase::WaitingClientPrepared;
		if (m_sendPrepare)
		{
			m_sendPrepare(transition.userId,
				{
					.token			= transition.barrierToken,
					.kind			= eWireBarrierKind::WorldPrepare,
					.correlation	= { .world = *transition.target, .mainRevision = transition.expectedRevision },
					.archetypeKey	= transition.target ? transition.target->instance.archetypeKey : WorldArchetypeKey{}
				});
		}
	}

	void ServerWorldTransitionCoordinator::SendClientCommit(UserContext& user)
	{
		auto& transition = *user.worldTransition.active;
		transition.phase = eWorldTransitionPhase::WaitingClientApplied;
		if (m_sendCommit)
		{
			m_sendCommit(transition.userId,
				{
					.token		 = transition.barrierToken,
					.correlation = { .world = *transition.target, .mainRevision = user.physicalWorld.revision }
				});
		}
	}

	void ServerWorldTransitionCoordinator::CompleteEnter(UserContext& user, const UserPhysicalWorldState& state)
	{
		const WorldTransitionState transition = *user.worldTransition.active;
		user.worldTransition.active.reset();

		SendResult(transition.userId, 
			{ 
				.kind			 = eWorldTransitionKind::Enter, 
				.requestId		 = transition.requestId, 
				.transitionToken = transition.token, 
				.state			 = state 
			});
		JAMNET_LOG_INFO("[EnterWorld] completed. userId={}, requestId={}, token={}, worldId={}",
			transition.userId, transition.requestId, transition.token.value,
			state.main ? state.main->worldId : kInvalidWorldId);

		if (m_mainChanged) m_mainChanged(transition.userId, state);
	}

	void ServerWorldTransitionCoordinator::CompleteLeave(UserContext& user, const UserPhysicalWorldState& state)
	{
		const WorldTransitionState transition = *user.worldTransition.active;
		const UserId userId = transition.userId;
		user.worldTransition.active.reset();

		SendResult(userId, 
			{ 
				.kind			 = eWorldTransitionKind::Leave, 
				.requestId		 = transition.requestId, 
				.transitionToken = transition.token, 
				.state			 = state 
			});
		
		if (m_mainChanged) 
			m_mainChanged(userId, state);
		if (user.tcp == kInvalidSessionId && user.udp == kInvalidSessionId && !user.physicalWorld.main)
			GetOrCreateUserShardState(CurrentShardLocalChecked()).FreeUserContext(userId);
	}

	void ServerWorldTransitionCoordinator::BeginFailure(UserContext& user, eWorldTransitionFailure failure, bool commandInFlight)
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

		if (transition.kind == eWorldTransitionKind::Enter && transition.target)
		{
			if (user.physicalWorld.main && *user.physicalWorld.main == *transition.target)
				CommitMain(transition.userId, user.physicalWorld.revision, transition.source);
			if (!SubmitRollbackTarget(user))
			{
				if (transition.sourceDetached && transition.source && SubmitRestoreSource(user))
					return;
				FinishFailure(user);
			}
			return;
		}

		if (transition.sourceDetached && transition.source)
		{
			if (!user.physicalWorld.main)
				CommitMain(transition.userId, user.physicalWorld.revision, transition.source);
			if (SubmitRestoreSource(user))
				return;
		}
		FinishFailure(user);
	}

	void ServerWorldTransitionCoordinator::FinishFailure(UserContext& user)
	{
		const WorldTransitionState transition = *user.worldTransition.active;
		const UserPhysicalWorldState state = user.physicalWorld;
		user.worldTransition.active.reset();

		SendResult(transition.userId, 
			{
				.kind			 = transition.kind, 
				.requestId		 = transition.requestId,
				.transitionToken = transition.token, 
				.failure		 = transition.terminalFailure, 
				.state			 = state 
			});

		if (transition.kind == eWorldTransitionKind::Enter)
			JAMNET_LOG_WARN("[EnterWorld] failed. userId={}, requestId={}, token={}, failure={}",
				transition.userId, transition.requestId, transition.token.value, static_cast<uint32>(transition.terminalFailure));

		if (user.tcp == kInvalidSessionId && user.udp == kInvalidSessionId && !user.physicalWorld.main)
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
			if (!entry.occupied || !entry.value.worldTransition.active)
				continue;
			const auto& transition = *entry.value.worldTransition.active;
			if (transition.deadlineNs <= nowNs)
				BeginFailure(entry.value, eWorldTransitionFailure::Timeout, true);
		}
	}

	void ServerWorldTransitionCoordinator::OnDisconnected(uint64 userId)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user)
			return;

		if (user->worldTransition.active)
			BeginFailure(*user, eWorldTransitionFailure::Disconnected, true);

		const std::optional<WorldRuntimeRef> main = user->physicalWorld.main;
		if (!main)
		{
			if (user->tcp == kInvalidSessionId && user->udp == kInvalidSessionId && !user->worldTransition.active)
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
			worldShard->Submit(Job([weak, userId, runtime = *main]()
				{
					GetOrCreateWorldShardState(CurrentShardLocalChecked()).DisconnectMember(userId, runtime);
					if (const auto coordinator = weak.lock())
					{
						coordinator->SubmitToUserShard(userId, [weak, userId, runtime]()
							{
								if (const auto resumed = weak.lock())
									resumed->CompleteDisconnectedWorldLeave(userId, runtime);
							});
					}
				}, eJobPriority::Control));
		}
	}

	void ServerWorldTransitionCoordinator::CompleteDisconnectedWorldLeave(uint64 userId, const WorldRuntimeRef& runtime)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (user)
			user->physicalWorld.ClearIfRuntime(runtime.worldId);

		if (user && user->tcp == kInvalidSessionId && user->udp == kInvalidSessionId
			&& !user->physicalWorld.main && !user->worldTransition.active)
			GetOrCreateUserShardState(CurrentShardLocalChecked()).FreeUserContext(userId);
	}
}
