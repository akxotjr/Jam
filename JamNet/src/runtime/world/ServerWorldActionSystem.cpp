#include "pch.h"
#include "jamnet/runtime/world/ServerWorldActionSystem.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardInvoke.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/utils/Clock.h"
#include "jamnet/runtime/ServerNetworkManager.h"
#include "jamnet/runtime/ServerSession.h"
#include "jamnet/runtime/world/WorldShardState.h"
#include "jamnet/runtime/world/DefaultWorldAssignmentPolicy.h"

#include "jamnet/sync/networld/ServerPhysicalWorld.h"
#include "jamnet/sync/networld/ServerVirtualWorld.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"

namespace jam::net
{
	namespace
	{
		void ApplyJoinedWorldMembership(UserContext& ctx, const WorldKey& key, eWorldKind kind, eWorldMembershipPresence presence)
		{
			for (auto& membership : ctx.worlds)
			{
				if (membership.key != key)
					continue;

				membership.kind		= kind;
				membership.role		= eWorldRole::None;
				membership.presence = presence;
				return;
			}

			ctx.worlds.push_back(WorldMembership
			{
				.key		= key,
				.kind		= kind,
				.role		= eWorldRole::None,
				.presence	= presence,
			});
		}

		void RemoveWorldMembership(UserContext& ctx, const WorldKey& key)
		{
			std::erase_if(ctx.worlds, [&key](const WorldMembership& membership)
				{
					return membership.key == key;
				});
		}

		void ApplyResolvedWorldMembership(UserContext& ctx, const WorldMembership& membership)
		{
			ApplyJoinedWorldMembership(ctx, membership.key, membership.kind, membership.presence);
			if (auto* joined = FindWorldMembership(ctx.worlds, membership.key))
			{
				joined->localWorldId = membership.localWorldId;
				joined->role		 = membership.role;
			}
		}

		eWorldRole ResolveAuthoritativeJoinRole(const UserContext& ctx, eWorldKind kind)
		{
			if (kind != eWorldKind::Physical)
				return eWorldRole::None;

			for (const WorldMembership& membership : ctx.worlds)
			{
				if (membership.kind == eWorldKind::Physical
					&& membership.role == eWorldRole::Main)
				{
					return eWorldRole::Auxiliary;
				}
			}

			return eWorldRole::Main;
		}

		eWorldRole ResolveAuthoritativeTransferRole(const UserContext& ctx, const WorldKey& sourceKey, eWorldKind targetKind)
		{
			if (const auto* sourceMembership = FindWorldMembership(ctx.worlds, sourceKey))
				return sourceMembership->role;
			return ResolveAuthoritativeJoinRole(ctx, targetKind);
		}
		
		void UpdateUserWorldMembership(uint64 userId, std::function<void(UserContext&)> fn)
		{
			if (userId == 0 || !fn)
				return;

			const uint16 shardIndex = GetUserShardIndex(userId);
			auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
			if (!shard)
				return;

			auto invoke = [userId, fn = std::move(fn)]() mutable
				{
					auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
					if (auto* ctx = userState.FindUserContext(userId))
						fn(*ctx);
				};

			InvokeOnShard(shard, [invoke = std::move(invoke)](ShardLocal&) mutable
				{
					invoke();
				}, eJobPriority::Control);
		}

		WorldActionResult MakeFailureResult(eWorldAction action, WorldActionExecFlags execFlags, eWorldActionReason reason, const WorldKey& sourceKey = {}, const WorldKey& targetKey = {})
		{
			return
			{
				.status	   = eWorldActionStatus::Failed,
				.reason	   = reason,
				.action	   = action,
				.execFlags = execFlags,
				.source	   = sourceKey,
				.target	   = targetKey,
			};
		}

		WorldActionResult MakeSuccessResult(eWorldAction action, WorldActionExecFlags execFlags, const WorldKey& sourceKey, const WorldKey& targetKey)
		{
			return
			{
				.status	   = eWorldActionStatus::Succeeded,
				.reason	   = eWorldActionReason::None,
				.action	   = action,
				.execFlags = execFlags,
				.source	   = sourceKey,
				.target	   = targetKey,
			};
		}

		void AppendUpsertMembershipDelta(WorldActionResult& result, const WorldMembership& membership)
		{
			result.membershipDeltas.push_back(WorldMembershipDelta
				{
					.op			= eWorldMembershipDeltaOp::Upsert,
					.membership	= membership,
				});
		}

		void AppendRemoveMembershipDelta(WorldActionResult& result, const WorldKey& key)
		{
			result.membershipDeltas.push_back(WorldMembershipDelta
				{
					.op = eWorldMembershipDeltaOp::Remove,
					.membership =
					{
						.key = key,
					},
				});
		}

		void PopulateMembershipDeltas(const UserContext& ctx, WorldActionResult& result)
		{
			switch (result.action)
			{
			case eWorldAction::AutoAssign:
			case eWorldAction::Join:
				if (const auto* membership = FindWorldMembership(ctx.worlds, result.target))
					AppendUpsertMembershipDelta(result, *membership);
				break;

			case eWorldAction::Leave:
				AppendRemoveMembershipDelta(result, result.source);
				break;

			case eWorldAction::Transfer:
				AppendRemoveMembershipDelta(result, result.source);
				if (const auto* membership = FindWorldMembership(ctx.worlds, result.target))
					AppendUpsertMembershipDelta(result, *membership);
				break;

			case eWorldAction::Promote:
				if (result.source.IsIssued() && result.source != result.target)
				{
					if (const auto* previous = FindWorldMembership(ctx.worlds, result.source))
						AppendUpsertMembershipDelta(result, *previous);
				}
				if (const auto* current = FindWorldMembership(ctx.worlds, result.target))
					AppendUpsertMembershipDelta(result, *current);
				break;
			}
		}

		WorldActionResult MakeRequesterResponseResult(const WorldActionResult& result)
		{
			if (result.action == eWorldAction::Leave || result.Failed())
				return result;

			WorldActionResult response = result;
			response.membershipDeltas.clear();
			response.worldRuntimeDeltas.clear();
			return response;
		}

		void AppendWorldRuntimeDelta(WorldActionResult& result, const WorldMeta& worldMeta)
		{
			if (worldMeta.kind != eWorldKind::Physical || !worldMeta.key.IsIssued())
				return;

			auto it = std::ranges::find(result.worldRuntimeDeltas, worldMeta.key, &WorldRuntimeDelta::key);
			if (it != result.worldRuntimeDeltas.end())
			{
				it->runtime = worldMeta.runtime;
				return;
			}

			result.worldRuntimeDeltas.push_back(WorldRuntimeDelta
				{
					.key = worldMeta.key,
					.runtime = worldMeta.runtime,
				});
		}

		void PopulateWorldRuntimeDeltas(const WorldDirectory& directory, WorldActionResult& result)
		{
			const auto appendIfPresent = [&directory, &result](const WorldKey& key)
				{
					if (!key.IsIssued())
						return;

					if (const std::optional<WorldMeta> worldMeta = directory.FindWorld(key); worldMeta.has_value())
						AppendWorldRuntimeDelta(result, *worldMeta);
				};

			switch (result.action)
			{
			case eWorldAction::AutoAssign:
			case eWorldAction::Join:
				appendIfPresent(result.target);
				break;

			case eWorldAction::Leave:
				appendIfPresent(result.source);
				break;

			case eWorldAction::Transfer:
				appendIfPresent(result.source);
				appendIfPresent(result.target);
				break;

			case eWorldAction::Promote:
				appendIfPresent(result.source);
				appendIfPresent(result.target);
				break;
			}
		}

		Packet MakeWorldActionNotificationPacket(const WorldActionResult& result)
		{
			flatbuffers::FlatBufferBuilder fbb(128);
			const auto root = result.CreateFb(fbb);
			fbb.Finish(root);

			return PacketBuilder::CreateCustomPacket(
				CustomPacketId::WORLD_ASSIGNMENT,
				PacketFlags::NONE,
				eChannel::RELIABLE_ORDERED,
				fbb.GetBufferPointer(),
				fbb.GetSize());
		}

		void SyncPhysicalWorldExecution(WorldShardState& state, const WorldKey& key)
		{
			const auto* worldMeta = state.FindAuthoritativeWorldEntry(key);
			auto* physical = dynamic_cast<PhysicalWorld*>(state.FindWorld(key));
			if (!worldMeta || !physical || worldMeta->kind != eWorldKind::Physical)
				return;

			physical->SetRuntimeState(worldMeta->runtime);
		}

		void CreateWorldOnShard(WorldShardState& state, WorldKey& key, RouteAssignment route, const WorldConfig& config, ServerNetworkManager* netManager, WorldDirectory* directory)
		{
			if (key.IsIssued())
				return;

			const LocalWorldId localWorldId = state.AllocLocalWorldId();
			if (localWorldId == kInvalidLocalWorldId) return;

			WorldConfig resolved = config;
			resolved.key = { key.descId, MakeWorldId(route.shardIndex, GetLocalWorldLocalIndex(localWorldId), GetLocalWorldGeneration(localWorldId)) };
			key = resolved.key;

			std::unique_ptr<WorldBase> world;

			if (resolved.desc.kind == eWorldKind::Virtual)
			{
				world = std::make_unique<ServerVirtualWorld>(resolved);
			}
			else
			{
				auto physicalWorld = std::make_unique<ServerPhysicalWorld>(resolved);
				if (netManager)
				{
					if (netManager->GetPhysicsFacotryProvider() && !resolved.desc.physicsProfile.empty())
					{
						if (auto phys = netManager->GetPhysicsFacotryProvider()(resolved.desc.physicsProfile))
							physicalWorld->SetPhysicsFacade(std::move(phys));
					}
					else if (netManager->GetPhysicsFacotry())
					{
						if (auto phys = netManager->GetPhysicsFacotry()())
							physicalWorld->SetPhysicsFacade(std::move(phys));
					}

					physicalWorld->SetLevelPath(resolved.desc.levelKey);
				}
				world = std::move(physicalWorld);
			}

			world->SetLocalWorldId(localWorldId);
			state.RegisterWorld(resolved, localWorldId);
			if (directory)
			{
				if (const auto* worldMeta = state.FindAuthoritativeWorldEntry(resolved.key))
					directory->PublishWorld(*worldMeta);
			}
			if (!world->Init() || !state.AdoptWorld(std::move(world)))
			{
				if (directory)
					directory->RemoveWorld(resolved.key);
				state.FreeLocalWorldId(localWorldId);
				key.worldId = kInvalidNetWorldId;
				return;
			}
		}

	}

	

	ServerWorldActionSystem::ServerWorldActionSystem()
	{
		m_policy   = std::make_unique<DefaultWorldAssignmentPolicy>();
	}


	void ServerWorldActionSystem::Init(ServerNetworkManager* owner)
	{
		m_netManager = owner;
		if (!m_netManager) return;

		m_asset = WorldDescIO::LoadWorldDescAsset(m_netManager->m_config.worldAssetPath);
			
		if (m_policy)
		{
			m_policy->BindWorldDirectory(&m_directory);
			m_policy->BindWorldTemplateAsset(&m_asset);
		}
	}

	void ServerWorldActionSystem::Shutdown()
	{
		m_netManager = nullptr;
		m_asset = {};
	}

	void ServerWorldActionSystem::SetWorldAssignmentPolicy(std::unique_ptr<IWorldAssignmentPolicy> policy)
	{
		m_policy = std::move(policy);
		if (m_policy)
		{
			m_policy->BindWorldDirectory(&m_directory);
			m_policy->BindWorldTemplateAsset(&m_asset);
		}
	}

	void ServerWorldActionSystem::Execute(WorldActionRequest req)
	{
		if (!m_netManager || req.principalId == 0 || !m_policy)
		{
			if (req.onResponse)
				req.onResponse(MakeFailureResult(req.action, {}, eWorldActionReason::InvalidArgument, req.source, req.target));
			return;
		}

		// Server-side world actions must run on the owning user shard.
		// The current call path is ServerUdpSession::OnWorldActionRequest, which already
		// executes on that shard and allows direct UserContext access below.
		const WorldActionPlan plan = m_policy->PlanAction(req);
		if (!plan.Executable())
		{
			if (req.onResponse)
				req.onResponse(MakeFailureResult(req.action, plan.execFlags, plan.reason, plan.source, plan.target));
			return;
		}

		ExecutePlan(std::move(req), plan);
	}

	void ServerWorldActionSystem::ExecutePlan(WorldActionRequest req, const WorldActionPlan& plan)
	{
		const bool doCreate   = HasWorldActionExec(plan.execFlags, eWorldActionExecFlag::CreateTarget);
		const bool touchesTarget = plan.action != eWorldAction::Leave;

		WorldKey targetKey = plan.target;
		if (touchesTarget && !targetKey.IsValid())
		{
			if (req.onResponse)
				req.onResponse(MakeFailureResult(req.action, plan.execFlags, eWorldActionReason::InvalidArgument, plan.source, targetKey));
			return;
		}

		WorldActionPlan resolvedPlan = plan;
		resolvedPlan.target = targetKey;

		if (resolvedPlan.action == eWorldAction::AutoAssign || resolvedPlan.action == eWorldAction::Join)
		{
			const bool started = doCreate
				? StartCreateTargetWorldAction(req, resolvedPlan)
				: StartJoinWorld(req, resolvedPlan);
			if (!started)
			{
				if (req.onResponse)
					req.onResponse(MakeFailureResult(req.action, resolvedPlan.execFlags, eWorldActionReason::TargetUnavailable, resolvedPlan.source, resolvedPlan.target));
			}
			return;
		}

		switch (resolvedPlan.action)
		{
		case eWorldAction::AutoAssign:
		case eWorldAction::Join:
			break;

		case eWorldAction::Leave:
			if (!StartLeaveWorld(req, resolvedPlan))
			{
				if (req.onResponse)
					req.onResponse(MakeFailureResult(req.action, resolvedPlan.execFlags, eWorldActionReason::TargetUnavailable, resolvedPlan.source, resolvedPlan.target));
			}
			return;

		case eWorldAction::Transfer:
			if (!(doCreate ? StartCreateTargetWorldAction(req, resolvedPlan) : StartTransferWorld(req, resolvedPlan)))
			{
				if (req.onResponse)
					req.onResponse(MakeFailureResult(req.action, resolvedPlan.execFlags, eWorldActionReason::TargetUnavailable, resolvedPlan.source, resolvedPlan.target));
			}
			return;

		case eWorldAction::Promote:
			if (!StartPromoteWorld(req, resolvedPlan))
			{
				if (req.onResponse)
					req.onResponse(MakeFailureResult(req.action, resolvedPlan.execFlags, eWorldActionReason::TargetUnavailable, resolvedPlan.source, resolvedPlan.target));
			}
			return;

		default:
			if (req.onResponse)
				req.onResponse(MakeFailureResult(req.action, resolvedPlan.execFlags, eWorldActionReason::InvalidArgument, resolvedPlan.source, resolvedPlan.target));
			return;
		}
	}

	bool ServerWorldActionSystem::HasConflictingInFlightWorldAction(const UserContext& ctx, const WorldActionPlan& plan) const
	{
		(void)plan;
		return !ctx.inFlightWorldActions.empty();
	}

	bool ServerWorldActionSystem::StartCreateTargetWorldAction(WorldActionRequest req, WorldActionPlan plan)
	{
		const UserId userId = req.principalId;
		if (userId == kInvalidUserId || !plan.target.IsValid() || plan.target.IsIssued())
			return false;

		const WorldConfig config = ResolveWorldConfig(plan.target);
		if (!config.IsValid())
			return false;

		const RouteAssignment route = PlaceNewWorldRoute(config);
		auto targetShard = GLOBAL_EXEC.GetShard(route);
		auto userShard = GLOBAL_EXEC.GetShardFromIndex(GetUserShardIndex(userId));
		if (!targetShard || !userShard)
			return false;

		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return false;
		if (HasConflictingInFlightWorldAction(*userCtx, plan))
			return false;

		const uint64 txnId = m_nextInFlightActionId.fetch_add(1, std::memory_order_relaxed);
		InFlightWorldAction action{};
		action.txnId     = txnId;
		action.request   = req;
		action.plan      = plan;
		action.startedNs = NOW_NS();
		userCtx->inFlightWorldActions.emplace(txnId, std::move(action));

		bool shouldCreate = false;
		{
			std::scoped_lock lock(m_pendingCreateMutex);
			auto& pending = m_pendingCreatesByDesc[plan.target.descId];
			shouldCreate = pending.empty();
			pending.push_back(PendingCreateTargetAction
				{
					.txnId = txnId,
					.userId = userId,
					.request = std::move(req),
					.plan = plan,
				});
		}

		if (!shouldCreate)
			return true;

		targetShard->Submit(Job([this, descId = plan.target.descId, plan, route, config]() mutable
			{
				WorldKey createdKey = plan.target;
				CreateWorldOnShard(GetOrCreateWorldShardState(CurrentShardLocalChecked()), createdKey, route, config, m_netManager, &m_directory);

				std::vector<PendingCreateTargetAction> pendingActions;
				{
					std::scoped_lock lock(m_pendingCreateMutex);
					if (auto it = m_pendingCreatesByDesc.find(descId); it != m_pendingCreatesByDesc.end())
					{
						pendingActions = std::move(it->second);
						m_pendingCreatesByDesc.erase(it);
					}
				}

				for (PendingCreateTargetAction& pending : pendingActions)
				{
					auto pendingUserShard = GLOBAL_EXEC.GetShardFromIndex(GetUserShardIndex(pending.userId));
					if (!pendingUserShard)
					{
						if (pending.request.onResponse)
							pending.request.onResponse(MakeFailureResult(pending.request.action, pending.plan.execFlags, eWorldActionReason::TargetUnavailable, pending.plan.source, pending.plan.target));
						continue;
					}

					pendingUserShard->Submit(Job([this, pending = std::move(pending), createdKey]() mutable
						{
							auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
							if (UserContext* userCtx = userState.FindUserContext(pending.userId))
								userCtx->inFlightWorldActions.erase(pending.txnId);

							WorldActionPlan createdPlan = pending.plan;
							createdPlan.target = createdKey;
							bool started = false;
							if (createdKey.IsIssued())
							{
								WorldActionRequest request = pending.request;
								if (createdPlan.action == eWorldAction::AutoAssign || createdPlan.action == eWorldAction::Join)
									started = StartJoinWorld(std::move(request), createdPlan);
								else if (createdPlan.action == eWorldAction::Transfer)
									started = StartTransferWorld(std::move(request), createdPlan);
							}

							if (!started)
							{
								if (pending.request.onResponse)
									pending.request.onResponse(MakeFailureResult(pending.request.action, createdPlan.execFlags, eWorldActionReason::TargetUnavailable, createdPlan.source, createdPlan.target));
							}
						}, eJobPriority::Control));
				}
			}, eJobPriority::Control));
		return true;
	}

	bool ServerWorldActionSystem::StartJoinWorld(WorldActionRequest req, const WorldActionPlan& plan)
	{
		const UserId userId = req.principalId;
		if (userId == kInvalidUserId || !plan.target.IsIssued())
			return false;

		const WorldConfig config = ResolveWorldConfig(plan.target);
		if (!config.IsValid())
			return false;

		auto targetShard = ResolveWorldShard(plan.target);
		auto userShard = GLOBAL_EXEC.GetShardFromIndex(GetUserShardIndex(userId));
		if (!targetShard || !userShard)
			return false;

		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return false;
		if (HasConflictingInFlightWorldAction(*userCtx, plan))
			return false;

		const uint64 txnId = m_nextInFlightActionId.fetch_add(1, std::memory_order_relaxed);
		InFlightWorldAction action{};
		action.txnId     = txnId;
		action.request   = std::move(req);
		action.plan      = plan;
		action.startedNs = NOW_NS();
		if (const auto* previous = FindWorldMembership(userCtx->worlds, plan.target))
			action.previousTargetMembership = *previous;

		const eWorldRole joinRole = ResolveAuthoritativeJoinRole(*userCtx, config.desc.kind);
		WorldMembership optimisticMembership{};
		optimisticMembership.key = plan.target;
		optimisticMembership.kind = config.desc.kind;
		optimisticMembership.role = joinRole;
		optimisticMembership.presence = plan.resultingPresence;
		if (const std::optional<WorldMeta> worldMeta = m_directory.FindWorld(plan.target); worldMeta.has_value())
			optimisticMembership.localWorldId = worldMeta->localWorldId;

		ApplyResolvedWorldMembership(*userCtx, optimisticMembership);
		userCtx->inFlightWorldActions.emplace(txnId, std::move(action));
		PublishUserMembershipSnapshot(*userCtx, userId);

		const WorldTransferContext ctx
		{
			.userId			   = userId,
			.target			   = plan.target,
			.resultingPresence = plan.resultingPresence,
		};
		WorldUserContext user = MakeWorldUserContext(userId);
		targetShard->Submit(Job([this, userShard, userId, txnId, ctx, user, config, role = joinRole]() mutable
			{
				WorldMembership joinedMembership{};
				joinedMembership.role = role;
				eWorldActionReason failureReason = eWorldActionReason::None;
				const bool joined = ExecuteAddWorldMemberOnCurrentWorldShard(ctx, user, config, &joinedMembership, failureReason);

				userShard->Submit(Job([this, userId, txnId, joined, joinedMembership, failureReason]() mutable
					{
						if (joined)
							CompleteJoinWorld(userId, txnId, joinedMembership);
						else
							RollbackJoinWorld(userId, txnId, failureReason);
					}, eJobPriority::Control));
			}, eJobPriority::Control));
		return true;
	}

	void ServerWorldActionSystem::CompleteJoinWorld(UserId userId, uint64 txnId, WorldMembership membership)
	{
		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return;

		auto it = userCtx->inFlightWorldActions.find(txnId);
		if (it == userCtx->inFlightWorldActions.end())
			return;

		InFlightWorldAction action = std::move(it->second);
		userCtx->inFlightWorldActions.erase(it);

		ApplyResolvedWorldMembership(*userCtx, membership);
		PublishUserMembershipSnapshot(*userCtx, userId);

		WorldActionResult result = MakeSuccessResult(action.request.action, action.plan.execFlags, action.plan.source, action.plan.target);
		PopulateMembershipDeltas(*userCtx, result);
		PopulateWorldRuntimeDeltas(m_directory, result);

		if (action.request.onResponse)
			action.request.onResponse(MakeRequesterResponseResult(result));
		NotifyWorldActionChanged(result);
	}

	void ServerWorldActionSystem::RollbackJoinWorld(UserId userId, uint64 txnId, eWorldActionReason reason)
	{
		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return;

		auto it = userCtx->inFlightWorldActions.find(txnId);
		if (it == userCtx->inFlightWorldActions.end())
			return;

		InFlightWorldAction action = std::move(it->second);
		userCtx->inFlightWorldActions.erase(it);

		RemoveWorldMembership(*userCtx, action.plan.target);
		if (action.previousTargetMembership.has_value())
			ApplyResolvedWorldMembership(*userCtx, *action.previousTargetMembership);
		PublishUserMembershipSnapshot(*userCtx, userId);

		if (action.request.onResponse)
		{
			if (reason == eWorldActionReason::None)
				reason = eWorldActionReason::TargetUnavailable;
			action.request.onResponse(MakeFailureResult(action.request.action, action.plan.execFlags, reason, action.plan.source, action.plan.target));
		}
	}

	bool ServerWorldActionSystem::StartLeaveWorld(WorldActionRequest req, const WorldActionPlan& plan)
	{
		const UserId userId = req.principalId;
		if (userId == kInvalidUserId || !plan.source.IsIssued())
			return false;

		auto sourceShard = ResolveWorldShard(plan.source);
		auto userShard = GLOBAL_EXEC.GetShardFromIndex(GetUserShardIndex(userId));
		if (!sourceShard || !userShard)
			return false;

		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return false;
		if (HasConflictingInFlightWorldAction(*userCtx, plan))
			return false;

		const auto* previous = FindWorldMembership(userCtx->worlds, plan.source);
		if (!previous)
			return false;

		const uint64 txnId = m_nextInFlightActionId.fetch_add(1, std::memory_order_relaxed);
		InFlightWorldAction action{};
		action.txnId     = txnId;
		action.request   = std::move(req);
		action.plan      = plan;
		action.previousSourceMembership = *previous;
		action.startedNs = NOW_NS();

		RemoveWorldMembership(*userCtx, plan.source);
		userCtx->inFlightWorldActions.emplace(txnId, std::move(action));
		PublishUserMembershipSnapshot(*userCtx, userId);

		const WorldTransferContext ctx
		{
			.userId			= userId,
			.source			= plan.source,
			.sourcePresence	= plan.sourcePresence,
		};
		sourceShard->Submit(Job([this, userShard, userId, txnId, ctx]() mutable
			{
				eWorldActionReason failureReason = eWorldActionReason::None;
				const bool removed = ExecuteRemoveWorldMemberOnCurrentWorldShard(ctx, userId, failureReason);

				userShard->Submit(Job([this, userId, txnId, removed, failureReason]() mutable
					{
						if (removed)
							CompleteLeaveWorld(userId, txnId);
						else
							RollbackLeaveWorld(userId, txnId, failureReason);
					}, eJobPriority::Control));
			}, eJobPriority::Control));
		return true;
	}

	void ServerWorldActionSystem::CompleteLeaveWorld(UserId userId, uint64 txnId)
	{
		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return;

		auto it = userCtx->inFlightWorldActions.find(txnId);
		if (it == userCtx->inFlightWorldActions.end())
			return;

		InFlightWorldAction action = std::move(it->second);
		userCtx->inFlightWorldActions.erase(it);

		if (HasWorldActionExec(action.plan.execFlags, eWorldActionExecFlag::DestroySource)
			&& action.plan.source.IsIssued()
			&& action.plan.source != action.plan.target)
		{
			DestroyWorld(action.plan.source);
		}

		WorldActionResult result = MakeSuccessResult(action.request.action, action.plan.execFlags, action.plan.source, action.plan.target);
		PopulateMembershipDeltas(*userCtx, result);
		PopulateWorldRuntimeDeltas(m_directory, result);

		if (action.request.onResponse)
			action.request.onResponse(MakeRequesterResponseResult(result));
		NotifyWorldActionChanged(result);
	}

	void ServerWorldActionSystem::RollbackLeaveWorld(UserId userId, uint64 txnId, eWorldActionReason reason)
	{
		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return;

		auto it = userCtx->inFlightWorldActions.find(txnId);
		if (it == userCtx->inFlightWorldActions.end())
			return;

		InFlightWorldAction action = std::move(it->second);
		userCtx->inFlightWorldActions.erase(it);

		if (action.previousSourceMembership.has_value())
			ApplyResolvedWorldMembership(*userCtx, *action.previousSourceMembership);
		PublishUserMembershipSnapshot(*userCtx, userId);

		if (action.request.onResponse)
		{
			if (reason == eWorldActionReason::None)
				reason = eWorldActionReason::TargetUnavailable;
			action.request.onResponse(MakeFailureResult(action.request.action, action.plan.execFlags, reason, action.plan.source, action.plan.target));
		}
	}

	bool ServerWorldActionSystem::StartTransferWorld(WorldActionRequest req, const WorldActionPlan& plan)
	{
		const UserId userId = req.principalId;
		if (userId == kInvalidUserId || !plan.source.IsIssued() || !plan.target.IsIssued())
			return false;

		const WorldConfig config = ResolveWorldConfig(plan.target);
		auto sourceShard = ResolveWorldShard(plan.source);
		auto targetShard = ResolveWorldShard(plan.target);
		auto userShard = GLOBAL_EXEC.GetShardFromIndex(GetUserShardIndex(userId));
		if (!config.IsValid() || !sourceShard || !targetShard || !userShard)
			return false;

		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return false;
		if (HasConflictingInFlightWorldAction(*userCtx, plan))
			return false;

		const uint64 txnId = m_nextInFlightActionId.fetch_add(1, std::memory_order_relaxed);
		InFlightWorldAction action{};
		action.txnId = txnId;
		action.request = std::move(req);
		action.plan = plan;
		if (const auto* sourceMembership = FindWorldMembership(userCtx->worlds, plan.source))
			action.previousSourceMembership = *sourceMembership;
		if (const auto* targetMembership = FindWorldMembership(userCtx->worlds, plan.target))
			action.previousTargetMembership = *targetMembership;
		action.startedNs = NOW_NS();

		const WorldTransferContext ctx
		{
			.userId			   = userId,
			.source			   = plan.source,
			.target			   = plan.target,
			.sourcePresence    = plan.sourcePresence,
			.resultingPresence = plan.resultingPresence,
		};

		WorldUserContext user = MakeWorldUserContext(userId);
		const eWorldRole transferRole = ResolveAuthoritativeTransferRole(*userCtx, plan.source, config.desc.kind);
		WorldMembership optimisticMembership{};
		optimisticMembership.key = plan.target;
		optimisticMembership.kind = config.desc.kind;
		optimisticMembership.role = transferRole;
		optimisticMembership.presence = plan.resultingPresence;
		if (const std::optional<WorldMeta> worldMeta = m_directory.FindWorld(plan.target); worldMeta.has_value())
			optimisticMembership.localWorldId = worldMeta->localWorldId;

		RemoveWorldMembership(*userCtx, plan.source);
		ApplyResolvedWorldMembership(*userCtx, optimisticMembership);
		userCtx->inFlightWorldActions.emplace(txnId, std::move(action));
		PublishUserMembershipSnapshot(*userCtx, userId);

		SubmitTransferPrepareOut(userId, txnId, ctx, user, config, transferRole);
		return true;
	}

	void ServerWorldActionSystem::SubmitTransferPrepareOut(
		UserId userId,
		uint64 txnId,
		WorldTransferContext ctx,
		WorldUserContext user,
		WorldConfig config,
		eWorldRole transferRole)
	{
		auto sourceShard = ResolveWorldShard(ctx.source);
		if (!sourceShard)
		{
			FailTransferWorld(userId, txnId, eWorldActionReason::TargetUnavailable);
			return;
		}

		sourceShard->Submit(Job([this, userId, txnId, ctx, user = user, config, transferRole]() mutable
			{
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				auto* host = dynamic_cast<WorldMembershipHost*>(state.FindWorld(ctx.source));
				if (!host)
				{
					SubmitTransferRollbackSourceAndFail(userId, txnId, ctx, {}, eWorldActionReason::TargetUnavailable);
					return;
				}

				auto payload = host->PrepareTransferOut(ctx);
				SubmitTransferPrepareInAndAdd(userId, txnId, ctx, std::move(user), config, transferRole, std::move(payload));
			}, eJobPriority::Control));
	}

	void ServerWorldActionSystem::SubmitTransferPrepareInAndAdd(
		UserId userId,
		uint64 txnId,
		WorldTransferContext ctx,
		WorldUserContext user,
		WorldConfig config,
		eWorldRole transferRole,
		std::shared_ptr<WorldTransferPayload> payload)
	{
		auto targetShard = ResolveWorldShard(ctx.target);
		if (!targetShard)
		{
			SubmitTransferRollbackSourceAndFail(userId, txnId, ctx, std::move(payload), eWorldActionReason::TargetUnavailable);
			return;
		}

		targetShard->Submit(Job([this, userId, txnId, ctx, user = std::move(user), config, transferRole, payload = std::move(payload)]() mutable
			{
				eWorldActionReason reason = eWorldActionReason::None;
				WorldMembership targetMembership{};
				targetMembership.role = transferRole;

				bool targetAdded = false;
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				auto* host = dynamic_cast<WorldMembershipHost*>(state.FindWorld(ctx.target));
				if (!host)
				{
					reason = eWorldActionReason::TargetUnavailable;
				}
				else if (TryPrepareTransferTargetReservationOnShard(state, ctx, *host, payload, reason))
				{
					targetAdded = ExecuteAddWorldMemberOnCurrentWorldShard(ctx, std::move(user), config, &targetMembership, reason);
					if (!targetAdded)
						ExecuteRollbackTransferInOnCurrentWorldShard(ctx, payload, false);
				}

				if (targetAdded)
					SubmitTransferRemoveSource(userId, txnId, ctx, std::move(payload), targetMembership);
				else
					SubmitTransferRollbackSourceAndFail(userId, txnId, ctx, std::move(payload), reason);
			}, eJobPriority::Control));
	}

	void ServerWorldActionSystem::SubmitTransferRemoveSource(
		UserId userId,
		uint64 txnId,
		WorldTransferContext ctx,
		std::shared_ptr<WorldTransferPayload> payload,
		WorldMembership targetMembership)
	{
		auto sourceShard = ResolveWorldShard(ctx.source);
		if (!sourceShard)
		{
			SubmitTransferRollbackTargetAndFail(userId, txnId, ctx, std::move(payload), eWorldActionReason::TargetUnavailable, true);
			return;
		}

		sourceShard->Submit(Job([this, userId, txnId, ctx, payload = std::move(payload), targetMembership]() mutable
			{
				eWorldActionReason reason = eWorldActionReason::None;
				if (ExecuteRemoveWorldMemberOnCurrentWorldShard(ctx, userId, reason))
				{
					if (auto* host = dynamic_cast<WorldMembershipHost*>(GetOrCreateWorldShardState(CurrentShardLocalChecked()).FindWorld(ctx.source)))
						host->CommitTransferOut(ctx, payload);
					SubmitTransferCommitTarget(userId, txnId, ctx, std::move(payload), targetMembership);
					return;
				}

				if (auto* host = dynamic_cast<WorldMembershipHost*>(GetOrCreateWorldShardState(CurrentShardLocalChecked()).FindWorld(ctx.source)))
					host->RollbackTransferOut(ctx, payload);
				SubmitTransferRollbackTargetAndFail(userId, txnId, ctx, std::move(payload), reason, true);
			}, eJobPriority::Control));
	}

	void ServerWorldActionSystem::SubmitTransferCommitTarget(
		UserId userId,
		uint64 txnId,
		WorldTransferContext ctx,
		std::shared_ptr<WorldTransferPayload> payload,
		WorldMembership targetMembership)
	{
		auto targetShard = ResolveWorldShard(ctx.target);
		if (!targetShard)
		{
			FailTransferWorld(userId, txnId, eWorldActionReason::TargetUnavailable);
			return;
		}

		targetShard->Submit(Job([this, userId, txnId, ctx, payload = std::move(payload), targetMembership]() mutable
			{
				if (auto* host = dynamic_cast<WorldMembershipHost*>(GetOrCreateWorldShardState(CurrentShardLocalChecked()).FindWorld(ctx.target)))
					host->CommitTransferIn(ctx, payload);
				CompleteTransferWorld(userId, txnId, targetMembership);
			}, eJobPriority::Control));
	}

	void ServerWorldActionSystem::SubmitTransferRollbackSourceAndFail(
		UserId userId,
		uint64 txnId,
		WorldTransferContext ctx,
		std::shared_ptr<WorldTransferPayload> payload,
		eWorldActionReason reason)
	{
		auto sourceShard = ResolveWorldShard(ctx.source);
		if (!sourceShard)
		{
			FailTransferWorld(userId, txnId, reason);
			return;
		}

		sourceShard->Submit(Job([this, userId, txnId, ctx, payload = std::move(payload), reason]() mutable
			{
				if (payload)
				{
					if (auto* host = dynamic_cast<WorldMembershipHost*>(GetOrCreateWorldShardState(CurrentShardLocalChecked()).FindWorld(ctx.source)))
						host->RollbackTransferOut(ctx, payload);
				}
				FailTransferWorld(userId, txnId, reason);
			}, eJobPriority::Control));
	}

	void ServerWorldActionSystem::SubmitTransferRollbackTargetAndFail(
		UserId userId,
		uint64 txnId,
		WorldTransferContext ctx,
		std::shared_ptr<WorldTransferPayload> payload,
		eWorldActionReason reason,
		bool targetMemberAdded)
	{
		auto targetShard = ResolveWorldShard(ctx.target);
		if (!targetShard)
		{
			FailTransferWorld(userId, txnId, reason);
			return;
		}

		targetShard->Submit(Job([this, userId, txnId, ctx, payload = std::move(payload), reason, targetMemberAdded]() mutable
			{
				ExecuteRollbackTransferInOnCurrentWorldShard(ctx, payload, targetMemberAdded);
				FailTransferWorld(userId, txnId, reason);
			}, eJobPriority::Control));
	}

	void ServerWorldActionSystem::CompleteTransferWorld(UserId userId, uint64 txnId, WorldMembership membership)
	{
		const uint16 userShardIndex = GetUserShardIndex(userId);
		if (auto* local = CurrentShardLocal(); !local || local->shardIndex != userShardIndex)
		{
			if (auto userShard = GLOBAL_EXEC.GetShardFromIndex(userShardIndex))
			{
				userShard->Submit(Job([this, userId, txnId, membership]() mutable
					{
						CompleteTransferWorld(userId, txnId, membership);
					}, eJobPriority::Control));
			}
			return;
		}

		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return;

		auto it = userCtx->inFlightWorldActions.find(txnId);
		if (it == userCtx->inFlightWorldActions.end())
			return;

		InFlightWorldAction action = std::move(it->second);
		userCtx->inFlightWorldActions.erase(it);

		ApplyResolvedWorldMembership(*userCtx, membership);
		PublishUserMembershipSnapshot(*userCtx, userId);

		if (HasWorldActionExec(action.plan.execFlags, eWorldActionExecFlag::DestroySource)
			&& action.plan.source.IsIssued()
			&& action.plan.source != action.plan.target)
		{
			DestroyWorld(action.plan.source);
		}

		WorldActionResult result = MakeSuccessResult(action.request.action, action.plan.execFlags, action.plan.source, action.plan.target);
		PopulateMembershipDeltas(*userCtx, result);
		PopulateWorldRuntimeDeltas(m_directory, result);

		if (action.request.onResponse)
			action.request.onResponse(MakeRequesterResponseResult(result));
		NotifyWorldActionChanged(result);
	}

	void ServerWorldActionSystem::FailTransferWorld(UserId userId, uint64 txnId, eWorldActionReason reason)
	{
		const uint16 userShardIndex = GetUserShardIndex(userId);
		if (auto* local = CurrentShardLocal(); !local || local->shardIndex != userShardIndex)
		{
			if (auto userShard = GLOBAL_EXEC.GetShardFromIndex(userShardIndex))
			{
				userShard->Submit(Job([this, userId, txnId, reason]()
					{
						FailTransferWorld(userId, txnId, reason);
					}, eJobPriority::Control));
			}
			return;
		}

		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return;

		auto it = userCtx->inFlightWorldActions.find(txnId);
		if (it == userCtx->inFlightWorldActions.end())
			return;

		InFlightWorldAction action = std::move(it->second);
		userCtx->inFlightWorldActions.erase(it);
		RemoveWorldMembership(*userCtx, action.plan.target);
		if (action.previousTargetMembership.has_value())
			ApplyResolvedWorldMembership(*userCtx, *action.previousTargetMembership);
		if (action.previousSourceMembership.has_value())
			ApplyResolvedWorldMembership(*userCtx, *action.previousSourceMembership);
		PublishUserMembershipSnapshot(*userCtx, userId);

		if (reason == eWorldActionReason::None)
			reason = eWorldActionReason::TargetUnavailable;
		if (action.request.onResponse)
			action.request.onResponse(MakeFailureResult(action.request.action, action.plan.execFlags, reason, action.plan.source, action.plan.target));
	}

	bool ServerWorldActionSystem::StartPromoteWorld(WorldActionRequest req, const WorldActionPlan& plan)
	{
		const UserId userId = req.principalId;
		if (userId == kInvalidUserId || !plan.target.IsIssued())
			return false;

		auto targetShard = ResolveWorldShard(plan.target);
		auto sourceShard = plan.source.IsIssued() && plan.source != plan.target ? ResolveWorldShard(plan.source) : nullptr;
		auto userShard = GLOBAL_EXEC.GetShardFromIndex(GetUserShardIndex(userId));
		if (!targetShard || !userShard || (plan.source.IsIssued() && plan.source != plan.target && !sourceShard))
			return false;

		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return false;
		if (HasConflictingInFlightWorldAction(*userCtx, plan))
			return false;

		const uint64 txnId = m_nextInFlightActionId.fetch_add(1, std::memory_order_relaxed);
		InFlightWorldAction action{};
		action.txnId = txnId;
		action.request = std::move(req);
		action.plan = plan;
		if (const auto* sourceMembership = FindWorldMembership(userCtx->worlds, plan.source))
			action.previousSourceMembership = *sourceMembership;
		if (const auto* targetMembership = FindWorldMembership(userCtx->worlds, plan.target))
			action.previousTargetMembership = *targetMembership;
		action.startedNs = NOW_NS();

		if (plan.source.IsIssued() && plan.source != plan.target)
		{
			if (auto* previous = FindWorldMembership(userCtx->worlds, plan.source))
			{
				previous->role     = eWorldRole::Auxiliary;
				previous->presence = eWorldMembershipPresence::Passive;
			}
		}
		if (auto* current = FindWorldMembership(userCtx->worlds, plan.target))
		{
			current->role     = eWorldRole::Main;
			current->presence = eWorldMembershipPresence::Active;
		}
		userCtx->inFlightWorldActions.emplace(txnId, std::move(action));
		PublishUserMembershipSnapshot(*userCtx, userId);

		auto promoteTarget = [this, userShard, userId, txnId, target = plan.target]()
			{
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				state.PromoteMemberPresence(target);
				SyncPublishedWorldState(state, target);
				userShard->Submit(Job([this, userId, txnId]()
					{
						CompletePromoteWorld(userId, txnId);
					}, eJobPriority::Control));
			};

		if (sourceShard)
		{
			sourceShard->Submit(Job([this, targetShard, promoteTarget = std::move(promoteTarget), source = plan.source]() mutable
				{
					auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
					state.DemoteMemberPresence(source);
					SyncPublishedWorldState(state, source);
					targetShard->Submit(Job(std::move(promoteTarget), eJobPriority::Control));
				}, eJobPriority::Control));
		}
		else
		{
			targetShard->Submit(Job(std::move(promoteTarget), eJobPriority::Control));
		}
		return true;
	}

	void ServerWorldActionSystem::CompletePromoteWorld(UserId userId, uint64 txnId)
	{
		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return;

		auto it = userCtx->inFlightWorldActions.find(txnId);
		if (it == userCtx->inFlightWorldActions.end())
			return;

		InFlightWorldAction action = std::move(it->second);
		userCtx->inFlightWorldActions.erase(it);

		if (action.plan.source.IsIssued() && action.plan.source != action.plan.target)
		{
			if (auto* previous = FindWorldMembership(userCtx->worlds, action.plan.source))
			{
				previous->role     = eWorldRole::Auxiliary;
				previous->presence = eWorldMembershipPresence::Passive;
			}
		}
		if (auto* current = FindWorldMembership(userCtx->worlds, action.plan.target))
		{
			current->role     = eWorldRole::Main;
			current->presence = eWorldMembershipPresence::Active;
		}
		PublishUserMembershipSnapshot(*userCtx, userId);

		WorldActionResult result = MakeSuccessResult(action.request.action, action.plan.execFlags, action.plan.source, action.plan.target);
		PopulateMembershipDeltas(*userCtx, result);
		PopulateWorldRuntimeDeltas(m_directory, result);

		if (action.request.onResponse)
			action.request.onResponse(MakeRequesterResponseResult(result));
		NotifyWorldActionChanged(result);
	}

	void ServerWorldActionSystem::FailPromoteWorld(UserId userId, uint64 txnId, eWorldActionReason reason)
	{
		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* userCtx = userState.FindUserContext(userId);
		if (!userCtx)
			return;

		auto it = userCtx->inFlightWorldActions.find(txnId);
		if (it == userCtx->inFlightWorldActions.end())
			return;

		InFlightWorldAction action = std::move(it->second);
		userCtx->inFlightWorldActions.erase(it);

		if (action.previousSourceMembership.has_value())
			ApplyResolvedWorldMembership(*userCtx, *action.previousSourceMembership);
		if (action.previousTargetMembership.has_value())
			ApplyResolvedWorldMembership(*userCtx, *action.previousTargetMembership);
		PublishUserMembershipSnapshot(*userCtx, userId);

		if (action.request.onResponse)
		{
			if (reason == eWorldActionReason::None)
				reason = eWorldActionReason::TargetUnavailable;
			action.request.onResponse(MakeFailureResult(action.request.action, action.plan.execFlags, reason, action.plan.source, action.plan.target));
		}
	}

	void ServerWorldActionSystem::OnSessionBundleChanged(uint64 userId)
	{
		if (userId == 0 || !m_netManager)
			return;

		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* ctx = userState.FindUserContext(userId);
		if (!ctx || ctx->worlds.empty())
			return;

		const WorldUserContext user = MakeWorldUserContext(userId);
		for (const WorldMembership& membership : ctx->worlds)
		{
			if (!membership.key.IsIssued())
				continue;

			SubmitWorldHostJob(membership.key, [user](WorldMembershipHost& host) mutable
				{
					host.UpdateMemberContext(user);
				});
		}
	}

	void ServerWorldActionSystem::OnSessionUnregistered(uint64 userId)
	{
		if (userId == 0)
			return;

		const auto membershipEntry = m_directory.FindUserMembershipEntry(userId);
		UpdateUserWorldMembership(userId, [userId](UserContext& ctx)
			{
				ctx.worlds.clear();

				if (ctx.tcp == kInvalidSessionId && ctx.udp == kInvalidSessionId)
				{
					auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
					userState.FreeUserContext(userId);
				}
			});
		RemoveUserMembershipSnapshot(userId);

		if (!membershipEntry)
			return;

		for (const WorldMembership& membership : membershipEntry->memberships)
		{
			if (!membership.key.IsIssued())
				continue;

			const WorldKey key = membership.key;
			const uint16 shardIndex = GetWorldShardIndex(key.worldId);
			auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
			if (shard)
			{
				shard->Submit(Job([this, key, userId, presence = membership.presence]()
					{
						auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
						if (auto* world = dynamic_cast<WorldMembershipHost*>(state.FindWorld(key)))
						{
							TryRemoveMemberFromWorldOnShard(state, key, userId, presence, *world);
						}
					}, eJobPriority::Control));
			}
		}
	}

	void ServerWorldActionSystem::OnWorldDestroyed(const WorldKey& key)
	{
		RemovePublishedWorld(key);
	}

	void ServerWorldActionSystem::Multicast(const WorldKey& key, Packet packet)
	{
		SubmitWorldHostJob(key, [packet = std::move(packet)](WorldMembershipHost& host) mutable
			{
				host.Multicast(std::move(packet));
			});
	}

	void ServerWorldActionSystem::Broadcast(Packet packet)
	{
		GLOBAL_EXEC.ConveyAll(Job([pkt = std::move(packet)]()
			{
				const auto& L = CurrentShardLocalChecked();
				const auto state = L.worldState;
				for (const auto& slot : state->worldsById.entries)
				{
					if (auto* host = slot.object ? dynamic_cast<WorldMembershipHost*>(slot.object.get()) : nullptr)
						host->Multicast(pkt);
				}
			}));
	}

	void ServerWorldActionSystem::NotifyWorldActionChanged(const WorldActionResult& result)
	{
		if (!result.Succeeded())
			return;

		Packet packet = MakeWorldActionNotificationPacket(result);
		if (!packet.IsValid())
			return;

		if (result.source.IsIssued())
			Multicast(result.source, packet);
		if (result.target.IsIssued() && result.target != result.source)
			Multicast(result.target, std::move(packet));
	}

	void ServerWorldActionSystem::CreateWorld(INOUT WorldKey& key)
	{
		if (!m_netManager || !key.IsValid())
			return;

		WorldConfig config = ResolveWorldConfig(key);
		if (!config.IsValid())
			return;

		const RouteAssignment route = PlaceNewWorldRoute(config);
		auto shard = GLOBAL_EXEC.GetShard(route);
		if (!shard) return;

		InvokeOnShard(shard, [this, route, config, &key](ShardLocal& local) mutable
		{
			auto& state = GetOrCreateWorldShardState(local);
			CreateWorldOnShard(state, key, route, config, m_netManager, &m_directory);
		}, eJobPriority::Control);
	}

	void ServerWorldActionSystem::DestroyWorld(const WorldKey& key)
	{
		if (!key.IsIssued()) return;

		const uint16 shardIndex = GetWorldShardIndex(key.worldId);
		auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
		if (!shard) return;

		auto* directory = &m_directory;
		shard->Submit(Job([key, directory]()
		{
			auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
			if (directory)
				directory->RemoveWorld(key);
			if (const auto* entry = state.FindAuthoritativeWorldEntry(key))
				state.BeginDestroyWorld(entry->localWorldId, eMailboxCloseMode::Abort);

		}, eJobPriority::Control));
	}


	WorldConfig ServerWorldActionSystem::ResolveWorldConfig(const WorldKey& key) const
	{
		return key.IsValid() ? m_asset.MakeConfig(key) : WorldConfig{};
	}

	std::shared_ptr<ShardExecutor> ServerWorldActionSystem::ResolveWorldShard(const WorldKey& key) const
	{
		if (!key.IsIssued())
			return nullptr;

		return GLOBAL_EXEC.GetShardFromIndex(GetWorldShardIndex(key.worldId));
	}

	WorldUserContext ServerWorldActionSystem::MakeWorldUserContext(uint64 userId)
	{
		return
		{
			.userId   = userId,
			.joined   = true,
			.sessions = m_netManager ? m_netManager->GetSessionBundle(userId) : ServerSessionBundle{},
		};
	}

	RouteAssignment ServerWorldActionSystem::PlaceNewWorldRoute(const WorldConfig& config) const
	{
		RoutePlacementOptions placement{};
		if (config.desc.route.preferredShard != 0)
		{
			placement.affinity.preferredShard = config.desc.route.preferredShard;
			placement.affinity.hard = config.desc.route.hardAffinity;
		}

		return GLOBAL_EXEC.PlaceRoute(GLOBAL_EXEC.MakeRouteKey("WorldTemplate", config.key.descId), placement);
	}

	bool ServerWorldActionSystem::ExecuteAddWorldMemberOnCurrentWorldShard(const WorldTransferContext& ctx, WorldUserContext user, const WorldConfig& config, WorldMembership* outMembership, eWorldActionReason& failureReason)
	{
		auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
		auto* host = dynamic_cast<WorldMembershipHost*>(state.FindWorld(ctx.target));
		if (!host)
		{
			failureReason = eWorldActionReason::TargetUnavailable;
			return false;
		}
		if (!TryAddMemberToWorldOnShard(state, ctx, *host, std::move(user)))
		{
			failureReason = eWorldActionReason::MailboxClosed;
			return false;
		}

		if (outMembership)
		{
			const auto* worldMeta = state.FindAuthoritativeWorldEntry(ctx.target);
			if (!worldMeta)
			{
				failureReason = eWorldActionReason::TargetUnavailable;
				return false;
			}

			outMembership->key		  = ctx.target;
			outMembership->localWorldId = worldMeta->localWorldId;
			outMembership->kind		  = config.desc.kind;
			outMembership->presence	  = ctx.resultingPresence;
		}

		failureReason = eWorldActionReason::None;
		return true;
	}

	bool ServerWorldActionSystem::ExecuteRemoveWorldMemberOnCurrentWorldShard(const WorldTransferContext& ctx, uint64 userId, eWorldActionReason& failureReason)
	{
		auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
		auto* host = dynamic_cast<WorldMembershipHost*>(state.FindWorld(ctx.source));
		if (!host)
		{
			failureReason = eWorldActionReason::TargetUnavailable;
			return false;
		}
		if (!TryRemoveMemberFromWorldOnShard(state, ctx.source, userId, ctx.sourcePresence, *host))
		{
			failureReason = eWorldActionReason::MailboxClosed;
			return false;
		}

		failureReason = eWorldActionReason::None;
		return true;
	}

	void ServerWorldActionSystem::ExecuteRollbackTransferInOnCurrentWorldShard(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload, bool hadTargetMembership)
	{
		auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
		if (auto* host = dynamic_cast<WorldMembershipHost*>(state.FindWorld(ctx.target)))
		{
			host->RollbackTransferIn(ctx, payload);
			if (hadTargetMembership)
				TryRemoveMemberFromWorldOnShard(state, ctx.target, ctx.userId, ctx.resultingPresence, *host);
			else
			{
				ReleaseTransferTargetReservationOnShard(state, ctx.target);
				PublishAuthoritativeWorldEntry(state, ctx.target);
			}
		}
	}



	bool ServerWorldActionSystem::SubmitWorldJob(const WorldKey& key, std::function<void(WorldBase&)> job)
	{
		if (!key.IsIssued() || !job)
		{
			JAMNET_LOG_WARN("[ServerWorldAcitonSystem::SubmitWorldJob] WorldKey isn't issue or Job is nullptr");
			return false;
		}

		const uint16 shardIndex = GetWorldShardIndex(key.worldId);
		auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
		if (!shard)
		{
			JAMNET_LOG_WARN("[ServerWorldActionSystem::SubmitWorldJob] not found target world. world id= {} / shard index= {} ", key.worldId, shardIndex);
			return false;
		}

		auto invoke = [key, job = std::move(job)](ShardLocal& local) mutable
		{
			auto& state = GetOrCreateWorldShardState(local);
			if (auto* world = state.FindWorld(key))
				job(*world);
		};

		if (auto* local = CurrentShardLocal(); local && local->shardIndex == shardIndex)
		{
			invoke(*local);
			return true;
		}

		shard->Submit(Job([invoke = std::move(invoke)]() mutable { invoke(CurrentShardLocalChecked()); }, eJobPriority::Normal));
		return true;
	}


	void ServerWorldActionSystem::PublishWorldDirectoryEntry(const WorldMeta& entry)
	{
		if (!entry.IsValid())
			return;

		m_directory.PublishWorld(entry);
	}

	void ServerWorldActionSystem::PublishAuthoritativeWorldEntry(const WorldShardState& state, const WorldKey& key)
	{
		if (const auto* entry = state.FindAuthoritativeWorldEntry(key))
			PublishWorldDirectoryEntry(*entry);
	}

	void ServerWorldActionSystem::RemovePublishedWorld(const WorldKey& key)
	{
		if (!key.IsIssued())
			return;

		m_directory.RemoveWorld(key);
	}

	void ServerWorldActionSystem::PublishUserMembershipSnapshot(const UserContext& ctx, UserId userId)
	{
		UserMembershipSnapshotEntry entry{};
		entry.userId = userId;
		entry.memberships.reserve(ctx.worlds.size());
		for (const WorldMembership& membership : ctx.worlds)
			entry.memberships.push_back(membership);

		m_directory.PublishUserMemberships(entry);
	}

	void ServerWorldActionSystem::RemoveUserMembershipSnapshot(UserId userId)
	{
		if (userId == kInvalidUserId)
			return;

		m_directory.RemoveUserMemberships(userId);
	}

	void ServerWorldActionSystem::SyncPublishedWorldState(WorldShardState& state, const WorldKey& key)
	{
		SyncPhysicalWorldExecution(state, key);
		PublishAuthoritativeWorldEntry(state, key);
	}

	bool ServerWorldActionSystem::TryPrepareTransferTargetReservationOnShard(WorldShardState& state, const WorldTransferContext& ctx, WorldMembershipHost& host, const std::shared_ptr<WorldTransferPayload>& payload, eWorldActionReason& failureReason)
	{
		if (!state.TryReserveMemberSlot(ctx.target))
		{
			failureReason = eWorldActionReason::CapacityExceeded;
			return false;
		}

		if (host.PrepareTransferIn(ctx, payload))
		{
			PublishAuthoritativeWorldEntry(state, ctx.target);
			failureReason = eWorldActionReason::None;
			return true;
		}

		host.RollbackTransferIn(ctx, payload);
		state.ReleaseMemberSlot(ctx.target);
		PublishAuthoritativeWorldEntry(state, ctx.target);
		failureReason = eWorldActionReason::MailboxClosed;
		return false;
	}

	void ServerWorldActionSystem::ReleaseTransferTargetReservationOnShard(WorldShardState& state, const WorldKey& key)
	{
		state.ReleaseMemberSlot(key);
	}

	bool ServerWorldActionSystem::TryAddMemberToWorldOnShard(WorldShardState& state, const WorldTransferContext& ctx, WorldMembershipHost& host, WorldUserContext user)
	{
		const bool hasPreparedReservation = ctx.source.IsIssued();
		if (!hasPreparedReservation && !state.TryReserveMemberSlot(ctx.target))
			return false;

		if (ctx.resultingPresence == eWorldMembershipPresence::Active)
			state.PromoteMemberPresence(ctx.target);

		// WorldShardState owns authoritative membership/runtime counts.
		// WorldMembershipHost only reflects that decided state into world-local execution.
		SyncPublishedWorldState(state, ctx.target);
		if (host.AddMember(user))
			return true;

		if (ctx.resultingPresence == eWorldMembershipPresence::Active)
			state.DemoteMemberPresence(ctx.target);
		if (!hasPreparedReservation)
			state.ReleaseMemberSlot(ctx.target);
		
		SyncPublishedWorldState(state, ctx.target);
		
		return false;
	}

	bool ServerWorldActionSystem::TryRemoveMemberFromWorldOnShard(WorldShardState& state, const WorldKey& key, uint64 userId, eWorldMembershipPresence presence, WorldMembershipHost& host)
	{
		if (presence == eWorldMembershipPresence::Active)
			state.DemoteMemberPresence(key);

		state.ReleaseMemberSlot(key);
		SyncPublishedWorldState(state, key);
		
		if (host.RemoveMember(userId))
			return true;
		if (!state.TryReserveMemberSlot(key))
			return false;
		if (presence == eWorldMembershipPresence::Active)
			state.PromoteMemberPresence(key);

		SyncPublishedWorldState(state, key);
		return false;
	}

}
