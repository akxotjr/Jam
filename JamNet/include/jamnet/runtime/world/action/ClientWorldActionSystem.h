#pragma once

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardInvoke.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/runtime/AppRuntimeEvents.h"
#include "jamnet/runtime/UserContext.h"
#include "jamnet/runtime/world/data/WorldDataBootstrap.h"
#include "jamnet/runtime/world/core/WorldDirectory.h"
#include "jamnet/runtime/world/action/WorldTransferSubsystem.h"
#include "jamnet/runtime/world/action/IWorldActionSystem.h"
#include "jamnet/sync/networld/ClientPhysicalWorld.h"
#include "jamnet/sync/networld/ClientVirtualWorld.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace jam::net
{
	class ClientNetworkManager;

	class ClientWorldActionSystem : public IWorldActionSystem, public WorldTransferSubsystem::ITransferOps
	{
	public:
		explicit ClientWorldActionSystem() = default;
		~ClientWorldActionSystem() override = default;

		void			Init(ClientNetworkManager* owner);
		void			Shutdown() override;
		void			Execute(WorldActionRequest req) override;
		void			CreateWorld(INOUT WorldKey& key) override;
		void			DestroyWorld(const WorldKey& key) override;
		//bool			SubmitWorldJob(const WorldKey& key, std::function<void(WorldBase&)> job) override;
		bool			PrepareTransferOut(const WorldTransferContext& ctx, std::shared_ptr<WorldTransferPayload>& payload, eWorldActionReason& failureReason) override;
		bool			PrepareTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload, eWorldActionReason& failureReason) override;
		void			CommitTransferOut(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) override;
		void			RollbackTransferOut(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) override;
		void			CommitTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) override;
		void			RollbackTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) override;
		bool			AddWorldMember(const WorldTransferContext& ctx, WorldUserContext user, eWorldActionReason& failureReason) override;
		bool			RemoveWorldMember(const WorldTransferContext& ctx, uint64 userId, eWorldActionReason& failureReason) override;
		bool			InvokeTransferHost(const WorldKey& key, std::function<bool(WorldMembershipHost&)> fn, bool& invoked) override;
		LocalWorldId	ResolveLocalWorldId(NetWorldId worldId) const;
		void			DispatchWorldPacket(NetWorldId worldId, Packet packet);

		void			RequestSpawnActor(LocalWorldId worldId, const SpawnParams& params);
		void			RequestDespawnActor(LocalWorldId worldId, NetId netId);
		void			RequestPossessActor(LocalWorldId worldId, NetId netId);
		void			RequestUnpossessActor(LocalWorldId worldId, NetId netId);
		void			PushInput(LocalWorldId worldId, uint32 inputFlags, float pitch, float yaw, uint32 commandEpoch);
		void			PushInput(LocalWorldId worldId, const px::CharacterInput& input);
		void			SetLatestClickMoveSeq(LocalWorldId worldId, uint64 requestSeq);
		void			RequestClickMove(LocalWorldId worldId, const px::Vec3& from, const px::Vec3& dir, float maxRange, uint64 requestSeq, uint32 commandEpoch, float facingYaw);
		void			ShutdownWorld(LocalWorldId localWorldId);
		void			ClearWorlds(AccountId accountId);

		void			ApplyWorldActionResult(const WorldActionResult& result);

		bool SubmitWorldJob(const WorldKey& key, std::function<void(WorldBase&)> job) override { return false; }

	private:
		template <typename Fn>
		bool SubmitPhysicalWorldJob(LocalWorldId localWorldId, Fn&& fn)
		{
			static constexpr uint8 kMaxPendingWorldRetries = 8;

			auto it = m_physicalWorlds.find(localWorldId);
			if (it == m_physicalWorlds.end())
			{
				JAMNET_LOG_WARN("[SubmitPhysicalWorldJob] local world id={} is not exist in m_physicalWorlds", localWorldId);
				return false;
			}

			if (ClientPhysicalWorld* world = it->second.TryGet(); world && world->IsCurrentShardContext())
			{
				fn(*world);
				return true;
			}

			using FnType = std::decay_t<Fn>;
			auto sharedFn = std::make_shared<FnType>(std::forward<Fn>(fn));
			auto retryJob = std::make_shared<std::function<void(uint8)>>();
			std::weak_ptr<std::function<void(uint8)>> weakRetryJob = retryJob;

			*retryJob = [id = localWorldId, sharedFn, weakRetryJob](uint8 retry) mutable
				{
					auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
					if (auto* world = dynamic_cast<ClientPhysicalWorld*>(state.FindWorld(id)))
					{
						(*sharedFn)(*world);
						return;
					}

					if (retry >= kMaxPendingWorldRetries)
					{
						JAMNET_LOG_WARN("[SubmitPhysicalWorldJob] local world id= {}. not found in WorldShardState", id);
						return;
					}

					auto shard = GLOBAL_EXEC.GetShardFromIndex(GetLocalWorldShardIndex(id));
					if (!shard)
					{
						JAMNET_LOG_WARN("[SubmitPhysicalWorldJob] local world id= {}. failed to resolve world shard", id);
						return;
					}

					auto nextRetryJob = weakRetryJob.lock();
					if (!nextRetryJob)
						return;

					shard->Submit(Job([retryJob = std::move(nextRetryJob), retry]() mutable
						{
							(*retryJob)(static_cast<uint8>(retry + 1));
						}, eJobPriority::Normal));
				};

			return it->second.TryPost(Job([retryJob]() mutable
				{
					(*retryJob)(0);
				}));
		}

		template <typename Fn>
		bool SubmitMembershipHostJob(LocalWorldId localWorldId, Fn&& fn) const
		{
			if (localWorldId == kInvalidLocalWorldId)
				return false;

			auto shard = GLOBAL_EXEC.GetShardFromIndex(GetLocalWorldShardIndex(localWorldId));
			if (!shard)
				return false;

			auto invoke = [id = localWorldId, fn = std::forward<Fn>(fn)](ShardLocal& local) mutable
				{
					auto& state = GetOrCreateWorldShardState(local);
					if (auto* host = dynamic_cast<WorldMembershipHost*>(state.FindWorld(id)))
						fn(*host);
				};

			//if (auto* local = CurrentShardLocal(); local && local->shardIndex == GetLocalWorldShardIndex(localWorldId))
			//{
			//	invoke(*local);
			//	return true;
			//}

			shard->Submit(Job([invoke = std::move(invoke)]() mutable
				{
					invoke(CurrentShardLocalChecked());
				}, eJobPriority::Control));
			return true;
		}

		void PublishWorldMembershipEvent(AccountId accountId, UserId userId, eWorldMembershipChange change, const WorldMembershipView& membership) const;
		WorldUserContext BuildClientWorldUserContext() const;
		void ApplyMembershipHost(LocalWorldId localWorldId, std::function<void(WorldMembershipHost&)> fn) const;
		void ApplyPhysicalWorldRuntime(const WorldRuntimeDelta& delta);
		
		void EnsureClientWorld(const WorldKey& key, uint16 userShardIndex);
		void CacheWorldRef(const WorldKey& key, LocalWorldId localWorldId, const MailboxRef& mailboxRef, bool isPhysicalWorld);
		void EraseWorldRef(LocalWorldId localWorldId);
		
		void JoinWorld(const WorldMembership& membership);
		void LeaveWorld(const WorldKey& target);
		void TransferWorld(const WorldKey& source, const WorldMembership& membership);
		void PromoteWorld(const WorldMembership* sourceMembership, const WorldMembership& targetMembership);

		static WorldMembershipView		BuildMembershipView(const WorldMembership& membership);
		static eWorldMembershipChange	ResolveMembershipChange(eWorldAction action);
		static void						ApplyResolvedWorldMembership(UserContext& ctx, const WorldMembership& membership);
		static WorldMeta				BuildClientWorldMeta(const WorldConfig& config, LocalWorldId localWorldId);

	private:
		ClientNetworkManager*								m_owner		= nullptr;
		WorldDirectory										m_directory = {};
		WorldDataBootstrapBundle							m_worldData	= {};
		std::unique_ptr<WorldTransferSubsystem>				m_transfer	= nullptr;

		std::unordered_map<NetWorldId, LocalWorldId>		m_localByNet;
		std::unordered_map<LocalWorldId, WorldMeta>			m_worldMetas;
		std::unordered_map<LocalWorldId, ClientVxWorldRef>	m_virtualWorlds;
		std::unordered_map<LocalWorldId, ClientPxWorldRef>	m_physicalWorlds;
	};
}
