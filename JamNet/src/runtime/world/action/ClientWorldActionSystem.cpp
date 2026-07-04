#include "pch.h"
#include "jamnet/runtime/world/action/ClientWorldActionSystem.h"

#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardInvoke.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/RPCAPI.h"
#include "jamnet/runtime/actor/ActorArchetypesLoader.h"
#include "jamnet/runtime/ClientNetworkManager.h"
#include "jamnet/runtime/AppRuntimeEvents.h"
#include "jamnet/runtime/ClientSession.h"
#include "jamnet/runtime/world/data/ActorLevelsLoader.h"
#include "jamnet/sync/networld/ClientPhysicalWorld.h"
#include "jamnet/sync/networld/ClientVirtualWorld.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"

namespace jam::net
{
	namespace
	{
		const WorldMembershipDelta* FindMembershipDelta(const WorldActionResult& result, eWorldMembershipDeltaOp op, const WorldKey& key)
		{
			auto it = std::ranges::find_if(result.membershipDeltas, [op, &key](const WorldMembershipDelta& delta)
				{
					return delta.op == op && delta.membership.key == key;
				});
			return (it != result.membershipDeltas.end()) ? &(*it) : nullptr;
		}

	}

	void ClientWorldActionSystem::Init(ClientNetworkManager* owner)
	{
		m_owner = owner;
		if (!m_owner) return;

		m_worldData = WorldDataBootstrap::Load(WorldDataBootstrapPaths
			{
				.sharedDataCatalogPath = m_owner->m_config.sharedDataCatalogPath,
				.worldTemplatePath = m_owner->m_config.worldTemplatePath,
				.worldArchetypePath = m_owner->m_config.worldArchetypePath,
			});
		m_transfer = std::make_unique<WorldTransferSubsystem>();
	}

	void ClientWorldActionSystem::Shutdown()
	{
		m_localByNet.clear();
		m_worldMetas.clear();
		m_virtualWorlds.clear();
		m_physicalWorlds.clear();
		m_owner = nullptr;
		m_worldData = {};
		m_transfer.reset();
	}

	void ClientWorldActionSystem::Execute(WorldActionRequest req)
	{
		if (!m_owner)
			return;
		
		if (auto* udp = m_owner->GetUdpSession())
			udp->RequestWorldAction(req);
	}

	void ClientWorldActionSystem::CreateWorld(INOUT WorldKey& key)
	{
		if (!m_owner || !key.IsIssued())
			return;

		if (ResolveLocalWorldId(key.worldId) != kInvalidLocalWorldId)
			return;

		const uint16 userShardIndex = static_cast<uint16>(CurrentShardLocalChecked().shardIndex);
		EnsureClientWorld(key, userShardIndex);
	}

	void ClientWorldActionSystem::DestroyWorld(const WorldKey& key)
	{
		if (!m_owner || !key.IsIssued())
			return;

		const LocalWorldId localWorldId = ResolveLocalWorldId(key.worldId);
		if (localWorldId != kInvalidLocalWorldId)
		{
			auto& L = CurrentShardLocalChecked();
			auto& state = GetOrCreateUserShardState(L);
			if (auto* ctx = state.FindUserContextByAccount(m_owner->GetAccountId()))
			{
				if (auto* membership = FindWorldMembership(ctx->worlds, key))
					membership->localWorldId = kInvalidLocalWorldId;
			}
			ShutdownWorld(localWorldId);
		}
	}

	//bool ClientWorldActionSystem::SubmitWorldJob(const WorldKey& key, std::function<void(WorldBase&)> job)
	//{
	//	if (!m_owner || !key.IsIssued() || !job)
	//		return false;

	//	const LocalWorldId localWorldId = ResolveLocalWorldId(key.worldId);
	//	if (localWorldId == kInvalidLocalWorldId)
	//		return false;

	//	const uint16 shardIndex = GetLocalWorldShardIndex(localWorldId);
	//	auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
	//	if (!shard)
	//		return false;

	//	auto invoke = [id = localWorldId, job = std::move(job)](ShardLocal& local) mutable
	//		{
	//			auto& state = GetOrCreateWorldShardState(local);
	//			if (auto* world = state.FindWorld(id))
	//				job(*world);
	//		};

	//	if (auto* local = CurrentShardLocal(); local && local->shardIndex == shardIndex)
	//	{
	//		invoke(*local);
	//		return true;
	//	}

	//	shard->Submit(Job([invoke = std::move(invoke)]() mutable
	//		{
	//			invoke(CurrentShardLocalChecked());
	//		}));
	//	return true;
	//}

	bool ClientWorldActionSystem::PrepareTransferOut(const WorldTransferContext& ctx, std::shared_ptr<WorldTransferPayload>& payload, eWorldActionReason& failureReason)
	{
		return WorldTransferSubsystem::PrepareTransferOutDefault(*this, ctx, payload, failureReason);
	}

	bool ClientWorldActionSystem::PrepareTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload, eWorldActionReason& failureReason)
	{
		if (!ctx.target.IsIssued())
		{
			failureReason = eWorldActionReason::InvalidArgument;
			return false;
		}

		bool prepared = false;
		bool succeeded = false;
		ApplyMembershipHost(ResolveLocalWorldId(ctx.target.worldId), [&prepared, &succeeded, &ctx, &payload](WorldMembershipHost& host)
			{
				prepared = true;
				succeeded = host.PrepareTransferIn(ctx, payload);
			});
		failureReason = prepared
			? (succeeded ? eWorldActionReason::None : eWorldActionReason::MailboxClosed)
			: eWorldActionReason::TargetUnavailable;
		return prepared && succeeded;
	}

	void ClientWorldActionSystem::CommitTransferOut(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload)
	{
		WorldTransferSubsystem::CommitTransferOutDefault(*this, ctx, payload);
	}

	void ClientWorldActionSystem::RollbackTransferOut(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload)
	{
		WorldTransferSubsystem::RollbackTransferOutDefault(*this, ctx, payload);
	}

	void ClientWorldActionSystem::CommitTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload)
	{
		WorldTransferSubsystem::CommitTransferInDefault(*this, ctx, payload);
	}

	void ClientWorldActionSystem::RollbackTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload)
	{
		ApplyMembershipHost(ResolveLocalWorldId(ctx.target.worldId), [&ctx, &payload](WorldMembershipHost& host)
			{
				host.RollbackTransferIn(ctx, payload);
			});
	}

	bool ClientWorldActionSystem::AddWorldMember(const WorldTransferContext& ctx, WorldUserContext user, eWorldActionReason& failureReason)
	{
		if (!ctx.target.IsIssued())
		{
			failureReason = eWorldActionReason::InvalidArgument;
			return false;
		}

		bool invoked = false;
		bool succeeded = false;
		ApplyMembershipHost(ResolveLocalWorldId(ctx.target.worldId), [&invoked, &succeeded, user = user](WorldMembershipHost& host) mutable
			{
				invoked   = true;
				succeeded = host.AddMember(user);
			});
		failureReason = invoked
			? (succeeded ? eWorldActionReason::None : eWorldActionReason::MailboxClosed)
			: eWorldActionReason::TargetUnavailable;
		return invoked && succeeded;
	}

	bool ClientWorldActionSystem::RemoveWorldMember(const WorldTransferContext& ctx, uint64 userId, eWorldActionReason& failureReason)
	{
		if (!ctx.source.IsIssued() || userId == 0)
		{
			failureReason = eWorldActionReason::InvalidArgument;
			return false;
		}

		bool invoked = false;
		bool succeeded = false;
		ApplyMembershipHost(ResolveLocalWorldId(ctx.source.worldId), [&invoked, &succeeded, userId](WorldMembershipHost& host)
			{
				invoked = true;
				succeeded = host.RemoveMember(userId);
			});
		failureReason = invoked
			? (succeeded ? eWorldActionReason::None : eWorldActionReason::MailboxClosed)
			: eWorldActionReason::TargetUnavailable;
		return invoked && succeeded;
	}

	bool ClientWorldActionSystem::InvokeTransferHost(const WorldKey& key, std::function<bool(WorldMembershipHost&)> fn, bool& invoked)
	{
		invoked = false;
		if (!key.IsIssued() || !fn)
			return false;

		bool succeeded = false;
		ApplyMembershipHost(ResolveLocalWorldId(key.worldId), [&fn, &invoked, &succeeded](WorldMembershipHost& host)
			{
				invoked = true;
				succeeded = fn(host);
			});
		return succeeded;
	}

	LocalWorldId ClientWorldActionSystem::ResolveLocalWorldId(NetWorldId worldId) const
	{
		if (worldId == kInvalidNetWorldId)
			return kInvalidLocalWorldId;

		const auto it = m_localByNet.find(worldId);
		return (it != m_localByNet.end()) ? it->second : kInvalidLocalWorldId;
	}

	void ClientWorldActionSystem::DispatchWorldPacket(NetWorldId worldId, Packet packet)
	{
		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		const LocalWorldId localWorldId = ResolveLocalWorldId(worldId);
		if (localWorldId == kInvalidLocalWorldId)
		{
			if (view.IsValid() && (view.Id() == CustomPacketId::SNAPSHOT || view.Id() == CustomPacketId::LIFECYCLE))
			{
				JAMNET_LOG_WARN(
					"[ClientWorldActionSystem] Drop world packet due to unresolved local world. account={} user={} worldId={} packetId={}",
					m_owner ? m_owner->GetAccountId() : kInvalidAccountId,
					m_owner ? m_owner->GetUserId() : kInvalidUserId,
					worldId,
					static_cast<uint32>(view.Id()));
			}
			return;
		}

		if (!packet.IsValid())
			return;

		SubmitMembershipHostJob(localWorldId, [packet = std::move(packet)](WorldMembershipHost& host) mutable
			{
				host.HandleWorldPacket(kInvalidUserId, std::move(packet));
			});
	}

	void ClientWorldActionSystem::RequestSpawnActor(LocalWorldId worldId, const SpawnParams& params)
	{
		bool submiited= SubmitPhysicalWorldJob(worldId, [params](ClientPhysicalWorld& world)
			{
				world.SpawnActor(params);
			});

		if (!submiited)
			JAMNET_LOG_WARN("[ClientWorldActionSystem::RequestSpawnActor] local world id= {}, failed to SubmitPhysicalWorldJob.", worldId);
	}

	void ClientWorldActionSystem::RequestDespawnActor(LocalWorldId worldId, NetId netId)
	{
		SubmitPhysicalWorldJob(worldId, [netId](ClientPhysicalWorld& world)
			{
				world.DespawnActor(netId);
			});
	}

	void ClientWorldActionSystem::RequestPossessActor(LocalWorldId worldId, NetId netId)
	{
		SubmitPhysicalWorldJob(worldId, [netId](ClientPhysicalWorld& world)
			{
				world.RequestPossessActor(netId);
			});
	}

	void ClientWorldActionSystem::RequestUnpossessActor(LocalWorldId worldId, NetId netId)
	{
		SubmitPhysicalWorldJob(worldId, [netId](ClientPhysicalWorld& world)
			{
				world.RequestUnpossessActor(netId);
			});
	}

	void ClientWorldActionSystem::PushInput(LocalWorldId worldId, uint32 inputFlags, float pitch, float yaw, uint32 commandEpoch)
	{
		SubmitPhysicalWorldJob(worldId, [inputFlags, pitch, yaw, commandEpoch](ClientPhysicalWorld& world)
			{
				world.PushInput(inputFlags, yaw, pitch, commandEpoch);
			});
	}

	void ClientWorldActionSystem::PushInput(LocalWorldId worldId, const px::CharacterInput& input)
	{
		SubmitPhysicalWorldJob(worldId, [input](ClientPhysicalWorld& world)
			{
				world.PushInput(input);
			});
	}

	void ClientWorldActionSystem::SetLatestClickMoveSeq(LocalWorldId worldId, uint64 requestSeq)
	{
		auto it = m_physicalWorlds.find(worldId);
		if (it == m_physicalWorlds.end())
			return;

		SubmitPhysicalWorldJob(worldId, [requestSeq](ClientPhysicalWorld& world)
			{
				world.SetLatestClickMoveSeq(requestSeq);
			});
	}

	void ClientWorldActionSystem::RequestClickMove(LocalWorldId worldId, const px::Vec3& from, const px::Vec3& dir, float maxRange, uint64 requestSeq, uint32 commandEpoch, float facingYaw)
	{
		auto it = m_physicalWorlds.find(worldId);
		if (it == m_physicalWorlds.end())
			return;

		SubmitPhysicalWorldJob(worldId, [from, dir, maxRange, requestSeq, commandEpoch, facingYaw](ClientPhysicalWorld& world)
			{
				world.RequestClickMove(from, dir, maxRange, requestSeq, commandEpoch, facingYaw);
			});
	}

	void ClientWorldActionSystem::ShutdownWorld(LocalWorldId localWorldId)
	{
		if (!m_owner || localWorldId == kInvalidLocalWorldId)
			return;

		EraseWorldRef(localWorldId);

		auto shard = GLOBAL_EXEC.GetShardFromIndex(GetLocalWorldShardIndex(localWorldId));
		if (!shard)
			return;

		shard->Submit(Job([localWorldId]()
			{
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				state.BeginDestroyWorld(localWorldId, eMailboxCloseMode::Abort);
			}, eJobPriority::Control));
	}

	void ClientWorldActionSystem::ClearWorlds(AccountId accountId)
	{
		if (!m_owner || accountId == kInvalidAccountId)
			return;

		const uint16 shardIndex = m_owner->ResolveClientUserShardIndex();
		auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
		if (!shard)
			return;

		struct ClearWorldSnapshot
		{
			std::vector<LocalWorldId> localWorldIds;
			std::vector<WorldKey> worldKeys;
		};

		const auto collect = [accountId](ShardLocal& local)
			{
				ClearWorldSnapshot snapshot;
				auto& state = GetOrCreateUserShardState(local);
				if (auto* ctx = state.FindUserContextByAccount(accountId))
				{
					snapshot.localWorldIds.reserve(ctx->worlds.size());
					snapshot.worldKeys.reserve(ctx->worlds.size());
					for (const WorldMembership& membership : ctx->worlds)
					{
						snapshot.worldKeys.push_back(membership.key);
						if (membership.localWorldId != kInvalidLocalWorldId)
							snapshot.localWorldIds.push_back(membership.localWorldId);
					}
					ctx->worlds.clear();
				}
				return snapshot;
			};

		auto* local = CurrentShardLocal();
		ClearWorldSnapshot snapshot = local && local->shardIndex == shardIndex
			? collect(*local)
			: InvokeOnShard(shard, collect, eJobPriority::Control);

		std::vector<LocalWorldId>& localWorldIds = snapshot.localWorldIds;
		for (const WorldKey& key : snapshot.worldKeys)
		{
			if (!key.IsIssued())
				continue;

			const LocalWorldId localWorldId = ResolveLocalWorldId(key.worldId);
			if (localWorldId != kInvalidLocalWorldId)
				localWorldIds.push_back(localWorldId);
		}

		std::sort(localWorldIds.begin(), localWorldIds.end());
		localWorldIds.erase(std::unique(localWorldIds.begin(), localWorldIds.end()), localWorldIds.end());
		for (LocalWorldId localWorldId : localWorldIds)
			ShutdownWorld(localWorldId);
	}

	void ClientWorldActionSystem::ApplyWorldActionResult(const WorldActionResult& result)
	{
		if (!m_owner || !result.Succeeded())
			return;

		for (const WorldMembershipDelta& delta : result.membershipDeltas)
		{
			if (delta.op != eWorldMembershipDeltaOp::Upsert || !delta.membership.key.IsIssued())
				continue;

			WorldKey targetKey = delta.membership.key;
			CreateWorld(targetKey);
		}

		for (const WorldRuntimeDelta& delta : result.worldRuntimeDeltas)
		{
			if (!delta.key.IsIssued())
				continue;

			WorldKey key = delta.key;
			CreateWorld(key);
		}

		switch (result.action)
		{
		case eWorldAction::AutoAssign:
		case eWorldAction::Join:
			if (const auto* delta = FindMembershipDelta(result, eWorldMembershipDeltaOp::Upsert, result.target))
				JoinWorld(delta->membership);
			break;

		case eWorldAction::Leave:
			LeaveWorld(result.source);
			break;

		case eWorldAction::Transfer:
			if (const auto* delta = FindMembershipDelta(result, eWorldMembershipDeltaOp::Upsert, result.target))
				TransferWorld(result.source, delta->membership);
			break;

		case eWorldAction::Promote:
			{
				const auto* sourceDelta = result.source.IsIssued() && result.source != result.target
					? FindMembershipDelta(result, eWorldMembershipDeltaOp::Upsert, result.source)
					: nullptr;
				if (const auto* targetDelta = FindMembershipDelta(result, eWorldMembershipDeltaOp::Upsert, result.target))
					PromoteWorld(sourceDelta ? &sourceDelta->membership : nullptr, targetDelta->membership);
			}
			break;
		}

		for (const WorldRuntimeDelta& delta : result.worldRuntimeDeltas)
			ApplyPhysicalWorldRuntime(delta);

		if (result.execFlags.has_any(eWorldActionExecFlag::DestroySource))
		{
			if (!result.source.IsIssued())
				return;
			DestroyWorld(result.source);
		}
	}

	void ClientWorldActionSystem::PublishWorldMembershipEvent(AccountId accountId, UserId userId, eWorldMembershipChange change, const WorldMembershipView& membership) const
	{
		WorldMembershipEvent evt{};
		evt.accountId  = accountId;
		evt.userId     = userId;
		evt.change     = change;
		evt.membership = membership;
		GLOBAL_EVENTBUS_PUBLISH(evt);
	}

	WorldUserContext ClientWorldActionSystem::BuildClientWorldUserContext() const
	{
		return WorldUserContext
		{
			.userId = m_owner ? m_owner->GetUserId() : kInvalidUserId,
			.joined = true,
		};
	}

	void ClientWorldActionSystem::ApplyMembershipHost(LocalWorldId localWorldId, std::function<void(WorldMembershipHost&)> fn) const
	{
		if (!fn)
			return;

		SubmitMembershipHostJob(localWorldId, [fn = std::move(fn)](WorldMembershipHost& host) mutable
			{
				fn(host);
			});
	}

	void ClientWorldActionSystem::ApplyPhysicalWorldRuntime(const WorldRuntimeDelta& delta)
	{
		if (!delta.key.IsIssued())
			return;

		const LocalWorldId localWorldId = ResolveLocalWorldId(delta.key.worldId);
		if (localWorldId == kInvalidLocalWorldId)
			return;

		if (auto it = m_worldMetas.find(localWorldId); it != m_worldMetas.end())
		{
			it->second.runtime = delta.runtime;
			m_directory.PublishWorld(it->second);
		}

		SubmitPhysicalWorldJob(localWorldId, [runtime = delta.runtime](ClientPhysicalWorld& world)
			{
				world.SetRuntimeState(runtime);
			});
	}

	void ClientWorldActionSystem::EnsureClientWorld(const WorldKey& key, uint16 userShardIndex)
	{
		if (!m_owner || !key.IsIssued() || ResolveLocalWorldId(key.worldId) != kInvalidLocalWorldId)
			return;

		WorldConfig worldConfig = m_worldData.resolver.ResolveWorldConfig(key);
		if (!worldConfig.IsValid())
			return;

		std::unique_ptr<WorldBase> world;
		if (worldConfig.templateData.kind == eWorldKind::Virtual)
		{
			auto vworld = std::make_unique<ClientVirtualWorld>(worldConfig);
			vworld->SetAccountId(m_owner->GetAccountId());
			vworld->SetUserId(m_owner->GetUserId());
			world = std::move(vworld);
		}
		else
		{
			auto pworld = std::make_unique<ClientPhysicalWorld>(worldConfig);
			pworld->SetAccountId(m_owner->GetAccountId());
			pworld->SetUserId(m_owner->GetUserId());
			pworld->SetHeadless(m_owner->m_config.headlessWorld);
			pworld->SetSessionBundle(ClientSessionBundle
				{
					.tcp = m_owner->m_tcp,
					.udp = m_owner->m_udp,
				});
			if (!m_owner->m_config.headlessWorld && m_owner->m_config.physicsFactory)
			{
				if (auto physics = m_owner->m_config.physicsFactory())
				{
					physics->SetPhysicsAssetPath(worldConfig.templateData.physicsAssetPath);
					pworld->SetPhysicsFacade(std::move(physics));
				}
			}
			if (!worldConfig.templateData.actorArchetypeSetPath.empty())
				pworld->SetActorArchetypeDatabase(ActorArchetypesLoader::Load(worldConfig.templateData.actorArchetypeSetPath));
			if (!worldConfig.templateData.actorLevelPath.empty())
				pworld->SetActorLevelDatabase(ActorLevelsLoader::Load(worldConfig.templateData.actorLevelPath));
			world = std::move(pworld);
		}

		if (!world->Init())
			return;

		const LocalWorldId localWorldId = world->GetLocalWorldId();
		if (localWorldId == kInvalidLocalWorldId)
			return;
		const MailboxRef mailboxRef = world->GetMailboxRef();
		const bool isPhysicalWorld = dynamic_cast<ClientPhysicalWorld*>(world.get()) != nullptr;

		m_worldMetas[localWorldId] = BuildClientWorldMeta(worldConfig, localWorldId);
		m_directory.PublishWorld(m_worldMetas[localWorldId]);
		CacheWorldRef(key, localWorldId, mailboxRef, isPhysicalWorld);

		const uint16 worldShardIndex = GetLocalWorldShardIndex(localWorldId);
		auto shard = GLOBAL_EXEC.GetShardFromIndex(worldShardIndex);
		if (!shard)
		{
			EraseWorldRef(localWorldId);
			return;
		}

		const auto publishLocalWorldIdUpdate = [accountId = m_owner->GetAccountId(), userId = m_owner->GetUserId(), membershipKey = key, localWorldId](ShardLocal& local)
			{
				auto& userState = GetOrCreateUserShardState(local);
				if (auto* ctx = userState.FindUserContextByAccount(accountId))
				{
					if (auto* membership = FindWorldMembership(ctx->worlds, membershipKey))
					{
						membership->localWorldId = localWorldId;

						WorldMembershipEvent evt{};
						evt.accountId  = accountId;
						evt.userId     = userId;
						evt.change     = eWorldMembershipChange::Updated;
						evt.membership = BuildMembershipView(*membership);
						GLOBAL_EVENTBUS_PUBLISH(evt);
					}
				}
			};

		shard->Submit(Job([world = std::move(world), userShardIndex, publishLocalWorldIdUpdate]() mutable
			{
				auto& state = GetOrCreateWorldShardState(CurrentShardLocalChecked());
				const LocalWorldId localWorldId = world ? world->GetLocalWorldId() : kInvalidLocalWorldId;
				if (localWorldId == kInvalidLocalWorldId)
					return;

				if (!state.AdoptWorld(std::move(world)))
				{
					JAMNET_LOG_WARN("[ClientWorldActionSystem::EnsureClientWorld] world id= {}. failed to adopt world", localWorldId);
					state.FreeLocalWorldId(localWorldId);
					return;
				}

				//if (auto* current = CurrentShardLocal(); current && current->shardIndex == userShardIndex)
				//{
				//	publishLocalWorldIdUpdate(*current);
				//	return;
				//}

				if (auto userShard = GLOBAL_EXEC.GetShardFromIndex(userShardIndex))
				{
					userShard->Submit(Job([publishLocalWorldIdUpdate]() mutable
						{
							publishLocalWorldIdUpdate(CurrentShardLocalChecked());
						}, eJobPriority::Control));
				}
			}, eJobPriority::Control));

	}

	void ClientWorldActionSystem::CacheWorldRef(const WorldKey& key, LocalWorldId localWorldId, const MailboxRef& mailboxRef, bool isPhysicalWorld)
	{
		if (!key.IsIssued() || localWorldId == kInvalidLocalWorldId || !mailboxRef.IsValid())
			return;

		m_localByNet[key.worldId] = localWorldId;

		if (isPhysicalWorld)
		{
			ClientPxWorldRef ref{};
			ref.ownerId = localWorldId;
			ref.mailbox = mailboxRef;
			m_physicalWorlds[localWorldId] = ref;
			return;
		}

		ClientVxWorldRef ref{};
		ref.ownerId = localWorldId;
		ref.mailbox = mailboxRef;
		m_virtualWorlds[localWorldId] = ref;
	}

	void ClientWorldActionSystem::EraseWorldRef(LocalWorldId localWorldId)
	{
		if (auto it = m_worldMetas.find(localWorldId); it != m_worldMetas.end())
		{
			m_directory.RemoveWorld(it->second.key);
			m_worldMetas.erase(it);
		}

		for (auto it = m_localByNet.begin(); it != m_localByNet.end(); )
		{
			if (it->second == localWorldId)
				it = m_localByNet.erase(it);
			else
				++it;
		}
		m_physicalWorlds.erase(localWorldId);
		m_virtualWorlds.erase(localWorldId);
	}

	void ClientWorldActionSystem::JoinWorld(const WorldMembership& authoritativeMembership)
	{
		if (!m_owner || !authoritativeMembership.key.IsIssued())
			return;

		auto& L		= CurrentShardLocalChecked();
		auto& state = GetOrCreateUserShardState(L);
		auto* ctx	= state.EnsureUserContext(m_owner->GetAccountId());
		if (!ctx)
			return;

		WorldMembership membership = authoritativeMembership;
		membership.localWorldId = ResolveLocalWorldId(membership.key.worldId);

		ApplyMembershipHost(membership.localWorldId, [user = BuildClientWorldUserContext()](WorldMembershipHost& host) mutable
			{
				host.AddMember(user);
			});
		ApplyResolvedWorldMembership(*ctx, membership);
		PublishWorldMembershipEvent(m_owner->GetAccountId(), m_owner->GetUserId(), ResolveMembershipChange(eWorldAction::Join), BuildMembershipView(membership));
	}

	void ClientWorldActionSystem::LeaveWorld(const WorldKey& target)
	{
		if (!m_owner || !target.IsIssued())
			return;

		auto& L     = CurrentShardLocalChecked();
		auto& state = GetOrCreateUserShardState(L);
		auto* ctx   = state.EnsureUserContext(m_owner->GetAccountId());
		if (!ctx)
			return;

		if (WorldMembership* membership = FindWorldMembership(ctx->worlds, target))
		{
			ApplyMembershipHost(membership->localWorldId, [userId = m_owner->GetUserId()](WorldMembershipHost& host)
				{
					host.RemoveMember(userId);
				});
			PublishWorldMembershipEvent(m_owner->GetAccountId(), m_owner->GetUserId(), ResolveMembershipChange(eWorldAction::Leave), BuildMembershipView(*membership));
		}

		std::erase_if(ctx->worlds, [&target](const WorldMembership& membership) { return membership.key == target; });
	}

	void ClientWorldActionSystem::TransferWorld(const WorldKey& source, const WorldMembership& authoritativeMembership)
	{
		if (!m_owner || !source.IsIssued() || !authoritativeMembership.key.IsIssued())
			return;

		if (source == authoritativeMembership.key)
			return;

		auto& L     = CurrentShardLocalChecked();
		auto& state = GetOrCreateUserShardState(L);
		auto* ctx   = state.EnsureUserContext(m_owner->GetAccountId());
		if (!ctx)
			return;

		const WorldMembership* sourceMembership = FindWorldMembership(ctx->worlds, source);
		WorldUserContext user = BuildClientWorldUserContext();
		eWorldActionReason failureReason = eWorldActionReason::None;
		if (!m_transfer || !m_transfer->Execute(*this, WorldTransferContext
			{
				.userId = user.userId,
				.source = source,
				.target = authoritativeMembership.key,
			}, user, failureReason))
		{
			return;
		}

		const bool hadSourceMembership = sourceMembership != nullptr;
		std::erase_if(ctx->worlds, [&source](const WorldMembership& membership) { return membership.key == source; });

		WorldMembership membership = authoritativeMembership;
		membership.localWorldId = ResolveLocalWorldId(membership.key.worldId);
		ApplyResolvedWorldMembership(*ctx, membership);

		if (hadSourceMembership)
		{
			PublishWorldMembershipEvent(m_owner->GetAccountId(), m_owner->GetUserId(), eWorldMembershipChange::Left, WorldMembershipView
				{
					.key = source,
				});
		}
		PublishWorldMembershipEvent(m_owner->GetAccountId(), m_owner->GetUserId(), ResolveMembershipChange(eWorldAction::Transfer), BuildMembershipView(membership));
	}

	void ClientWorldActionSystem::PromoteWorld(const WorldMembership* sourceMembership, const WorldMembership& authoritativeTargetMembership)
	{
		if (!m_owner || !authoritativeTargetMembership.key.IsIssued())
			return;

		auto& L     = CurrentShardLocalChecked();
		auto& state = GetOrCreateUserShardState(L);
		auto* ctx   = state.EnsureUserContext(m_owner->GetAccountId());
		if (!ctx)
			return;

		WorldMembership* prev = sourceMembership ? FindWorldMembership(ctx->worlds, sourceMembership->key) : nullptr;
		WorldMembership* now  = FindWorldMembership(ctx->worlds, authoritativeTargetMembership.key);
		if (!now)
			return;

		if (prev && prev != now)
		{
			*prev = *sourceMembership;
			prev->localWorldId = ResolveLocalWorldId(prev->key.worldId);
			ApplyMembershipHost(prev->localWorldId, [user = BuildClientWorldUserContext()](WorldMembershipHost& host) mutable
				{
					host.UpdateMemberContext(user);
				});
			PublishWorldMembershipEvent(m_owner->GetAccountId(), m_owner->GetUserId(), eWorldMembershipChange::Updated, BuildMembershipView(*prev));
		}
		*now = authoritativeTargetMembership;
		now->localWorldId = ResolveLocalWorldId(now->key.worldId);
		ApplyMembershipHost(now->localWorldId, [user = BuildClientWorldUserContext()](WorldMembershipHost& host) mutable
			{
				host.UpdateMemberContext(user);
			});
		PublishWorldMembershipEvent(m_owner->GetAccountId(), m_owner->GetUserId(), ResolveMembershipChange(eWorldAction::Promote), BuildMembershipView(*now));
	}

	WorldMembershipView ClientWorldActionSystem::BuildMembershipView(const WorldMembership& membership)
	{
		return WorldMembershipView
		{
			.key = membership.key,
			.localWorldId = membership.localWorldId,
			.kind = membership.kind,
			.role = membership.role,
		};
	}

	eWorldMembershipChange ClientWorldActionSystem::ResolveMembershipChange(eWorldAction action)
	{
		switch (action)
		{
		case eWorldAction::Join:
		case eWorldAction::AutoAssign:
			return eWorldMembershipChange::Joined;
		case eWorldAction::Leave:
			return eWorldMembershipChange::Left;
		case eWorldAction::Transfer:
			return eWorldMembershipChange::Transferred;
		case eWorldAction::Promote:
			return eWorldMembershipChange::Promoted;
		default:
			return eWorldMembershipChange::Updated;
		}
	}

	void ClientWorldActionSystem::ApplyResolvedWorldMembership(UserContext& ctx, const WorldMembership& membership)
	{
		WorldMembership* joined = FindWorldMembership(ctx.worlds, membership.key);
		if (!joined)
		{
			ctx.worlds.push_back(membership);
			return;
		}

		joined->key		   = membership.key;
		joined->localWorldId = membership.localWorldId;
		joined->kind	   = membership.kind;
		joined->role	   = membership.role;
		joined->presence   = membership.presence;
	}

	WorldMeta ClientWorldActionSystem::BuildClientWorldMeta(const WorldConfig& config, LocalWorldId localWorldId)
	{
		WorldMeta entry{};
		entry.key = config.key;
		entry.localWorldId = localWorldId;
		entry.kind = config.templateData.kind;
		entry.group = config.templateData.group;
		entry.capacity = config.templateData.capacity;
		entry.runtime = (config.templateData.kind == eWorldKind::Physical)
			? ePhysicalWorldRuntimeState::Standby
			: ePhysicalWorldRuntimeState::Active;
		return entry;
	}

}
