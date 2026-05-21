#pragma once

#include "jamnet/runtime/world/IWorldActionSystem.h"
#include "jamnet/runtime/world/WorldBase.h"
#include "jamnet/runtime/world/WorldActionTypes.h"
#include "jamnet/runtime/world/WorldDirectory.h"
#include "jamnet/runtime/world/WorldDescAsset.h"
#include "jamnet/runtime/world/IWorldAssignmentPolicy.h"
#include "jamnet/sync/networld/ServerPhysicalWorld.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>


namespace jam
{
	class ShardExecutor;
}

namespace jam::net
{
	struct WorldMeta;
	class ServerNetworkManager;

	class ServerWorldActionSystem : public IWorldActionSystem
	{
	public:
		ServerWorldActionSystem();
		~ServerWorldActionSystem() override = default;

		void						Init(ServerNetworkManager* owner);
		void						Shutdown() override;
		void						SetWorldAssignmentPolicy(std::unique_ptr<IWorldAssignmentPolicy> policy);
		IWorldAssignmentPolicy*		GetWorldAssignmentPolicy() const { return m_policy.get(); }
		void						Execute(WorldActionRequest req) override;

		void						OnSessionBundleChanged(uint64 userId);
		void						OnSessionUnregistered(uint64 userId);
		void						OnWorldDestroyed(const WorldKey& key);

		void						Multicast(const WorldKey& key, Packet packet);
		void						Broadcast(Packet packet);

		void						CreateWorld(INOUT WorldKey& key) override;
		void						DestroyWorld(const WorldKey& key) override;

		bool						SubmitWorldJob(const WorldKey& key, std::function<void(WorldBase&)> job) override;
		template <typename Fn, typename MissingFn = std::nullptr_t>
		bool						SubmitPhysicalWorldJob(const WorldKey& key, Fn&& fn, MissingFn&& onMissing = nullptr)
		{
			return SubmitWorldJob(key, [fn = std::forward<Fn>(fn), onMissing = std::forward<MissingFn>(onMissing)](WorldBase& world) mutable
				{
					if (auto* physicalWorld = dynamic_cast<ServerPhysicalWorld*>(&world))
					{
						fn(*physicalWorld);
						return;
					}

					if constexpr (!std::is_same_v<std::decay_t<MissingFn>, std::nullptr_t>)
						onMissing();
				});
		}
		template <typename Fn, typename MissingFn = std::nullptr_t>
		bool						SubmitWorldHostJob(const WorldKey& key, Fn&& fn, MissingFn&& onMissing = nullptr)
		{
			return SubmitWorldJob(key, [fn = std::forward<Fn>(fn), onMissing = std::forward<MissingFn>(onMissing)](WorldBase& world) mutable
				{
					if (auto* host = dynamic_cast<WorldMembershipHost*>(&world))
					{
						fn(*host);
						return;
					}

					if constexpr (!std::is_same_v<std::decay_t<MissingFn>, std::nullptr_t>)
						onMissing();
				});
		}
	private:
		void						PublishWorldDirectoryEntry(const WorldMeta& entry);
		void						PublishAuthoritativeWorldEntry(const WorldShardState& state, const WorldKey& key);
		void						RemovePublishedWorld(const WorldKey& key);
		void						PublishUserMembershipSnapshot(const UserContext& ctx, UserId userId);
		void						RemoveUserMembershipSnapshot(UserId userId);
		void						SyncPublishedWorldState(WorldShardState& state, const WorldKey& key);
		std::shared_ptr<ShardExecutor> ResolveWorldShard(const WorldKey& key) const;
		bool						ExecuteAddWorldMemberOnCurrentWorldShard(const WorldTransferContext& ctx, WorldUserContext user, const WorldConfig& config, WorldMembership* outMembership, eWorldActionReason& failureReason);
		bool						ExecuteRemoveWorldMemberOnCurrentWorldShard(const WorldTransferContext& ctx, uint64 userId, eWorldActionReason& failureReason);
		void						ExecuteRollbackTransferInOnCurrentWorldShard(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload, bool hadTargetMembership);
		bool						TryPrepareTransferTargetReservationOnShard(WorldShardState& state, const WorldTransferContext& ctx, WorldMembershipHost& host, const std::shared_ptr<WorldTransferPayload>& payload, eWorldActionReason& failureReason);
		void						ReleaseTransferTargetReservationOnShard(WorldShardState& state, const WorldKey& key);
		bool						TryAddMemberToWorldOnShard(WorldShardState& state, const WorldTransferContext& ctx, WorldMembershipHost& host, WorldUserContext user);
		bool						TryRemoveMemberFromWorldOnShard(WorldShardState& state, const WorldKey& key, uint64 userId, eWorldMembershipPresence presence, WorldMembershipHost& host);
		void						ExecutePlan(WorldActionRequest req, const WorldActionPlan& plan);
		bool						HasConflictingInFlightWorldAction(const UserContext& ctx, const WorldActionPlan& plan) const;
		
		bool						StartCreateTargetWorldAction(WorldActionRequest req, WorldActionPlan plan);
		
		bool						StartJoinWorld(WorldActionRequest req, const WorldActionPlan& plan);
		void						CompleteJoinWorld(UserId userId, uint64 txnId, WorldMembership membership);
		void						RollbackJoinWorld(UserId userId, uint64 txnId, eWorldActionReason reason);
		
		bool						StartLeaveWorld(WorldActionRequest req, const WorldActionPlan& plan);
		void						CompleteLeaveWorld(UserId userId, uint64 txnId);
		void						RollbackLeaveWorld(UserId userId, uint64 txnId, eWorldActionReason reason);
		
		bool						StartTransferWorld(WorldActionRequest req, const WorldActionPlan& plan);
		void						CompleteTransferWorld(UserId userId, uint64 txnId, WorldMembership membership);
		void						FailTransferWorld(UserId userId, uint64 txnId, eWorldActionReason reason);
		void						SubmitTransferPrepareOut(UserId userId, uint64 txnId, WorldTransferContext ctx, WorldUserContext user, WorldConfig config, eWorldRole transferRole);
		void						SubmitTransferPrepareInAndAdd(UserId userId, uint64 txnId, WorldTransferContext ctx, WorldUserContext user, WorldConfig config, eWorldRole transferRole, std::shared_ptr<WorldTransferPayload> payload);
		void						SubmitTransferRemoveSource(UserId userId, uint64 txnId, WorldTransferContext ctx, std::shared_ptr<WorldTransferPayload> payload, WorldMembership targetMembership);
		void						SubmitTransferCommitTarget(UserId userId, uint64 txnId, WorldTransferContext ctx, std::shared_ptr<WorldTransferPayload> payload, WorldMembership targetMembership);
		void						SubmitTransferRollbackSourceAndFail(UserId userId, uint64 txnId, WorldTransferContext ctx, std::shared_ptr<WorldTransferPayload> payload, eWorldActionReason reason);
		void						SubmitTransferRollbackTargetAndFail(UserId userId, uint64 txnId, WorldTransferContext ctx, std::shared_ptr<WorldTransferPayload> payload, eWorldActionReason reason, bool targetMemberAdded);
		
		bool						StartPromoteWorld(WorldActionRequest req, const WorldActionPlan& plan);
		void						CompletePromoteWorld(UserId userId, uint64 txnId);
		void						FailPromoteWorld(UserId userId, uint64 txnId, eWorldActionReason reason);
		
		void						NotifyWorldActionChanged(const WorldActionResult& result);

		WorldConfig					ResolveWorldConfig(const WorldKey& key) const;
		WorldUserContext			MakeWorldUserContext(uint64 userId);
		RouteAssignment				PlaceNewWorldRoute(const WorldConfig& config) const;

	private:
		struct PendingCreateTargetAction
		{
			uint64				txnId = 0;
			UserId				userId = kInvalidUserId;
			WorldActionRequest	request = {};
			WorldActionPlan		plan = {};
		};

		ServerNetworkManager*						m_netManager = nullptr;
		std::unique_ptr<IWorldAssignmentPolicy>		m_policy	 = nullptr;

		WorldDirectory								m_directory	 = {};
		WorldDescAsset								m_asset		 = {};
		std::atomic<uint64>							m_nextInFlightActionId = 1;
		std::mutex									m_pendingCreateMutex;
		std::unordered_map<uint32, std::vector<PendingCreateTargetAction>> m_pendingCreatesByDesc;
	};
}
