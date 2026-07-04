#include "pch.h"
#include "jamnet/runtime/ServerSession.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/net/RPC.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/utils/Clock.h"

#include "jamnet/sync/networld/ServerPhysicalWorld.h"
#include "jamnet/sync/replication/ReplicationCodec.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"
#include "jamnet/runtime/ServerNetworkManager.h"
#include "jamnet/runtime/world/action/ServerWorldActionSystem.h"

namespace jam::net
{
	namespace
	{

		uint16 ResolveAccountShardIndex(AccountId accountId)
		{
			if (accountId == kInvalidAccountId)
				return static_cast<uint16>(kInvalidRouteShard);

			const RouteKey routeKey = GLOBAL_EXEC.MakeRouteKey(RouteDomain::From("BoundSession"), accountId);
			if (auto shard = GLOBAL_EXEC.GetShard(routeKey))
				return static_cast<uint16>(shard->GetIndex());

			return static_cast<uint16>(kInvalidRouteShard);
		}


		void BuildWorldAssignmentFlatBuffer(flatbuffers::FlatBufferBuilder& fbb, const WorldActionResult& res)
		{
			const auto root = res.CreateFb(fbb);
			fbb.Finish(root);
		}

		void SendWorldAssignmentResponse(entt::entity e, const WorldActionResult& res, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(128);
			BuildWorldAssignmentFlatBuffer(fbb, res);
			RPCSendResponse<fb::fbWorldActionRes>(e, fbb.GetBufferPointer(), fbb.GetSize(), requestId, eChannel::RELIABLE_ORDERED);
		}


		void SendSpawnActorResponse(entt::entity e, bool success, uint64 spawnReqId, NetId netId, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbSpawnActorRes(fbb, success, spawnReqId, netId.Raw());
			fbb.Finish(root);
			RPCSendResponse<fb::fbSpawnActorRes>(e, fbb.GetBufferPointer(), fbb.GetSize(), requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendDespawnActorResponse(entt::entity e, bool success, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbDespawnActorRes(fbb, success);
			fbb.Finish(root);
			RPCSendResponse<fb::fbDespawnActorRes>(e, fbb.GetBufferPointer(), fbb.GetSize(), requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendPossessActorResponse(entt::entity e, bool success, NetId netId, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbPossessActorRes(fbb, success, netId.Raw());
			fbb.Finish(root);
			RPCSendResponse<fb::fbPossessActorRes>(e, fbb.GetBufferPointer(), fbb.GetSize(), requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendUnpossessActorResponse(entt::entity e, bool success, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbUnpossessActorRes(fbb, success);
			fbb.Finish(root);
			RPCSendResponse<fb::fbUnpossessActorRes>(e, fbb.GetBufferPointer(), fbb.GetSize(), requestId, eChannel::RELIABLE_ORDERED);
		}

		WorldKey ResolveJoinedWorldKey(const UserContext& ctx, NetWorldId worldId)
		{
			if (worldId == kInvalidNetWorldId)
				return {};

			for (const WorldMembership& membership : ctx.worlds)
			{
				if (membership.key.worldId == worldId)
					return membership.key;
			}

			return {};
		}

		WorldKey ResolveRequestedWorldKey(UserId userId, NetWorldId worldId)
		{
			if (userId == kInvalidUserId || worldId == kInvalidNetWorldId)
				return {};

			auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
			if (const auto* ctx = state.FindUserContext(userId))
				return ResolveJoinedWorldKey(*ctx, worldId);
			return {};
		}
	}


	void ServerTcpSession::OnLinkEstablished()
	{
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ServerTcpSession established. ip: {} | port: {}", 
			GetAccountId(), 
			GetUserId(), 
			GetRemoteNetAddress().GetIpAddress(), 
			GetRemoteNetAddress().GetPort());

		FinalizeEstablishedSession();
		if (IsClosing())
			return;
		BootstrapRPC();
	}

	void ServerTcpSession::OnDisconnected()
	{
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ServerTcpSession disconnected", GetAccountId(), GetUserId());

		if (m_manager && m_userId != kInvalidUserId)
		{
			auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
			if (auto* ctx = state.FindUserContext(m_userId))
			{
				if (ctx->tcp == GetSessionId())
					ctx->tcp = kInvalidSessionId;
				ctx->udp = kInvalidSessionId;
				if (ctx->tcp == kInvalidSessionId && ctx->udp == kInvalidSessionId && ctx->worlds.empty())
					state.FreeUserContext(m_userId);
			}

			m_manager->ReleaseSession(m_userId);
		}
	}

	void ServerTcpSession::HandleCustomPacket(Packet packet)
	{
		if (!m_manager)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());

		const NetWorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidNetWorldId || m_userId == kInvalidUserId)
			return;

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		const auto* ctx = state.FindUserContext(m_userId);
		if (!ctx)
			return;

		const WorldKey targetWorldKey = ResolveJoinedWorldKey(*ctx, worldId);
		if (!targetWorldKey.IsIssued())
			return;

		if (auto* worldActionSystem = m_manager->GetWorldActionSystem())
		{
			worldActionSystem->SubmitWorldHostJob(targetWorldKey, [userId = m_userId, packet = std::move(packet)](WorldMembershipHost& host) mutable
				{
					host.HandleWorldPacket(userId, std::move(packet));
				});
		}
	}

	RuntimeId ServerTcpSession::ResolveServerTcpBindUserId(uint64 accountId)
	{
		if (accountId == kInvalidAccountId || !m_manager || !GetService())
			return kInvalidRuntimeId;

		auto& L = CurrentShardLocalChecked();
		auto& state = GetOrCreateUserShardState(L);
		UserContext* ctx = state.FindUserContextByAccount(accountId);
		if (!ctx)
			ctx = state.AllocUserContext(accountId);
		return ctx ? ctx->userId : kInvalidRuntimeId;
	}

	void ServerTcpSession::FinalizeEstablishedSession()
	{
		if (!m_manager || m_accountId == kInvalidAccountId || m_userId == kInvalidUserId || GetSessionId() == kInvalidSessionId)
		{
			Disconnect();
			return;
		}

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* ctx = state.FindUserContext(m_userId);

		if (!ctx || ctx->accountId != m_accountId)
		{
			Disconnect();
			return;
		}

		const SessionId prevTcp = ctx->tcp;
		ctx->tcp = GetSessionId();
		if (!m_manager->CacheTcpSession(m_userId, this))
		{
			ctx->tcp = prevTcp;
			Disconnect();
		}
	}

	void ServerTcpSession::BootstrapRPC()
	{
	}



	void ServerUdpSession::OnLinkEstablished()
	{
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ServerUdpSession established. ip: {} | port: {}",
			GetAccountId(),
			GetUserId(),
			GetRemoteNetAddress().GetIpAddress(),
			GetRemoteNetAddress().GetPort());

		FinalizeEstablishedSession();
		if (IsClosing())
			return;
		BootstrapRPC();
	}

	void ServerUdpSession::OnDisconnected()
	{
		JAMNET_LOG_INFO("[AccountId= {}, UserId = {}] ServerUdpSession disconnected", GetAccountId(), GetUserId());

		if (m_manager && m_userId != kInvalidUserId)
		{
			auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
			if (auto* ctx = state.FindUserContext(m_userId))
			{
				if (ctx->udp == GetSessionId())
					ctx->udp = kInvalidSessionId;
				if (ctx->tcp == kInvalidSessionId && ctx->udp == kInvalidSessionId && ctx->worlds.empty())
					state.FreeUserContext(m_userId);
			}

			m_manager->ReleaseUdpSession(m_userId, this);
		}
	}

	void ServerUdpSession::HandleCustomPacket(Packet packet)
	{
		if (!m_manager)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		const NetWorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidNetWorldId || m_userId == kInvalidUserId)
			return;

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		const auto* ctx = state.FindUserContext(m_userId);
		if (!ctx)
			return;

		const WorldKey targetWorldKey = ResolveJoinedWorldKey(*ctx, worldId);
		if (!targetWorldKey.IsIssued())
			return;

		if (auto* worldActionSystem = m_manager->GetWorldActionSystem())
		{
			worldActionSystem->SubmitWorldHostJob(targetWorldKey, [userId = m_userId, packet = std::move(packet)](WorldMembershipHost& host) mutable
				{
					host.HandleWorldPacket(userId, std::move(packet));
				});
		}
	}

	bool ServerUdpSession::ValidateServerUdpBindPrincipal(uint64 accountId, RuntimeId userId)
	{
		if (accountId == kInvalidAccountId || userId == kInvalidUserId || !m_manager || !GetService())
			return false;

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		const uint16 accountShardIndex = ResolveAccountShardIndex(accountId);
		UserContext* ctx = state.FindUserContext(userId);
		if (!ctx || ctx->accountId != accountId || ctx->tcp == kInvalidSessionId)
			return false;
		return GetUserShardIndex(ctx->userId) == accountShardIndex;
	}

	void ServerUdpSession::FinalizeEstablishedSession()
	{
		if (!m_manager || m_accountId == kInvalidAccountId || m_userId == kInvalidUserId || GetSessionId() == kInvalidSessionId)
		{
			Disconnect();
			return;
		}

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* ctx = state.FindUserContext(m_userId);
		if (!ctx || ctx->accountId != m_accountId || ctx->tcp == kInvalidSessionId)
		{
			Disconnect();
			return;
		}

		const SessionId prevUdp = ctx->udp;
		ctx->udp = GetSessionId();
		if (!m_manager->CacheUdpSession(m_userId, this))
		{
			ctx->udp = prevUdp;
			Disconnect();
			return;
		}
	}

	void ServerUdpSession::BootstrapRPC()
	{
		auto& R = CurrentShardLocalChecked().registry;
		const entt::entity e = GetEntity();
		if (e == entt::null || !R.valid(e))
			return;

		RPCRegisterRequest<fb::fbWorldActionReq>(R, e, this, &ServerUdpSession::OnWorldActionRequest);
		RPCRegisterRequest<fb::fbSpawnActorReq>(R, e, this, &ServerUdpSession::OnSpawnActorRequest);
		RPCRegisterRequest<fb::fbDespawnActorReq>(R, e, this, &ServerUdpSession::OnDespawnActorRequest);
		RPCRegisterRequest<fb::fbPossessActorReq>(R, e, this, &ServerUdpSession::OnPossessActorRequest);
		RPCRegisterRequest<fb::fbUnpossessActorReq>(R, e, this, &ServerUdpSession::OnUnpossessActorRequest);
	}


	void ServerUdpSession::OnWorldActionRequest(entt::entity e, const fb::fbWorldActionReq& req, uint32 requestId)
	{
		if (!m_manager)
		{
			const WorldActionRequest parsed = WorldActionRequest::FromFb(req, m_userId);
			WorldActionResult res
			{
				.status	= eWorldActionStatus::Failed,
				.action = parsed.action,
				.source = WorldKey{ req.src_archetype_key(), req.src_world_id() },
				.target = WorldKey{ req.target_archetype_key(), req.target_world_id() },
			};
			SendWorldAssignmentResponse(GetEntity(), res, requestId);
			return;
		}

		const SessionId sessionId = GetSessionId();
		WorldActionRequest actionReq = WorldActionRequest::FromFb(
			req,
			m_userId,
			[e, sessionId, requestId](WorldActionResult result) mutable
			{
				const auto shard = GLOBAL_EXEC.GetShardFromIndex(GetRuntimeShardIndex(sessionId));
				if (!shard) return;

				shard->Submit(Job([e, sessionId, result, requestId]() mutable
				{
					auto& state = GetOrCreateSessionShardState();
					auto* self = static_cast<ServerUdpSession*>(state.FindSession(sessionId));
					if (!self) return;

					JAM_ASSERT(e == self->GetEntity());

					SendWorldAssignmentResponse(self->GetEntity(), result, requestId);
				}));
			});

		m_manager->RequestWorldAction(std::move(actionReq));
	}

	void ServerUdpSession::OnSpawnActorRequest(entt::entity e, const fb::fbSpawnActorReq& req, uint32 requestId)
	{
		JAMNET_LOG_DEBUG("[ServerUdpSession::OnSpawnActorRequest] account id= {}, user id= {} request spawn actor", m_accountId, m_userId);

		const WorldKey targetWorldKey = ResolveRequestedWorldKey(m_userId, req.world_id());
		if (!m_manager || !targetWorldKey.IsIssued())
		{
			SendSpawnActorResponse(e, false, req.spawn_req_id(), NetId::Invalid(), requestId);
			return;
		}

		const ActorArchetypeKey actorArchetypeKey = ActorArchetypeKey::FromU64(req.actor_archetype_key());
		if (!IsValidAssetKey(actorArchetypeKey) || !req.pos() || !req.rot())
		{
			SendSpawnActorResponse(e, false, req.spawn_req_id(), NetId::Invalid(), requestId);
			return;
		}

		const UserId userId = m_userId;
		if (userId == kInvalidUserId)
		{
			SendSpawnActorResponse(e, false, req.spawn_req_id(), NetId::Invalid(), requestId);
			return;
		}

		if ((req.owner_user_id() != 0 && req.owner_user_id() != userId)
			|| (req.controller_user_id() != 0 && req.controller_user_id() != userId))
		{
			JAMNET_LOG_ERROR("Spawn request rejected due to principal mismatch. principal={}, owner={}, controller={}",
				userId, req.owner_user_id(), req.controller_user_id());
			SendSpawnActorResponse(e, false, req.spawn_req_id(), NetId::Invalid(), requestId);
			return;
		}

		SpawnParams params{};

		params.spawnId				= req.spawn_req_id();
		params.actorArchetypeKey	= actorArchetypeKey;
		params.owner				= (req.owner_user_id() != 0 || req.controller_user_id() != 0) ? userId : 0;
		params.controller			= (req.controller_user_id() != 0) ? userId : 0;
		params.targetNetId			= NetId::MakeRaw(req.target_net_id());
		params.desc.spawnSrc		= px::eSpawnSource::Runtime;
		params.desc.pose			= { .p = { req.pos()->x(), req.pos()->y(), req.pos()->z() }, .q = { req.rot()->x(), req.rot()->y(), req.rot()->z(), req.rot()->w() } };
		params.desc.team			= static_cast<uint16>(req.team_id());
		params.desc.part			= static_cast<uint8>(req.part_id());
		params.desc.role			= static_cast<uint8>(req.role_id());

		px::SpawnOverrideMask::Flag overrideMask{ req.override_mask() };

		if (px::IsRigidOverrideMask(overrideMask))
		{
			px::RigidSpawnOverrides overrides{};
			overrides.mask = overrideMask;

			if (overrideMask.has_any(px::SpawnOverrideMask::LINEAR_VEL) && req.linear_vel())
				overrides.linearVelocity = px::Vec3{ req.linear_vel()->x(), req.linear_vel()->y(), req.linear_vel()->z() };
			if (overrideMask.has_any(px::SpawnOverrideMask::ANGULAR_VEL) && req.angular_vel())
				overrides.angularVelocity = px::Vec3{ req.angular_vel()->x(), req.angular_vel()->y(), req.angular_vel()->z() };
			if (overrideMask.has_any(px::SpawnOverrideMask::LINEAR_DAMP))
				overrides.linearDamping = req.linear_damping();
			if (overrideMask.has_any(px::SpawnOverrideMask::ANGULAR_DAMP))
				overrides.angularDamping = req.angular_damping();

			params.desc.overrides = overrides;
		}
		else
		{
			px::CharacterSpawnOverrides overrides{};
			overrides.mask = overrideMask;

			if (overrideMask.has_any(px::SpawnOverrideMask::VIEW_YAW))
				overrides.yaw = req.yaw();
			if (overrideMask.has_any(px::SpawnOverrideMask::VIEW_PITCH))
				overrides.pitch = req.pitch();

			params.desc.overrides = overrides;
		}

		const SessionId sessionId = GetSessionId();
		const uint32 reqId		= requestId;
		const uint32 spawnReqId = req.spawn_req_id();

		auto* worldActionSystem = m_manager->GetWorldActionSystem();
		if (!worldActionSystem || !worldActionSystem->SubmitPhysicalWorldJob(targetWorldKey, [params, sessionId, e, reqId, spawnReqId](ServerPhysicalWorld& physicalWorld) mutable
			{
				physicalWorld.SpawnActorAsync(params, [sessionId, reqId, spawnReqId](NetId netId) mutable
					{
						const auto shard = GLOBAL_EXEC.GetShardFromIndex(GetRuntimeShardIndex(sessionId));
						if (!shard)
							return;

						shard->Submit(Job([sessionId, netId, spawnReqId, reqId]() mutable
							{
								auto& state = GetOrCreateSessionShardState();
								auto* self = static_cast<ServerUdpSession*>(state.FindSession(sessionId));
								if (!self)
									return;

								SendSpawnActorResponse(self->GetEntity(), netId.IsValid(), spawnReqId, netId, reqId);
							}));
					});
			}, [e, reqId, spawnReqId]()
			{
				SendSpawnActorResponse(e, false, spawnReqId, NetId::Invalid(), reqId);
			}))
		{
			SendSpawnActorResponse(e, false, req.spawn_req_id(), NetId::Invalid(), requestId);
		}
	}

	void ServerUdpSession::OnDespawnActorRequest(entt::entity e, const fb::fbDespawnActorReq& req, uint32 requestId)
	{
		const WorldKey targetWorldKey = ResolveRequestedWorldKey(m_userId, req.world_id());
		if (!m_manager || !targetWorldKey.IsIssued())
		{
			SendDespawnActorResponse(e, false, requestId);
			return;
		}

		const UserId userId = m_userId;
		const NetId  netId = NetId::MakeRaw(req.net_id());

		auto* worldActionSystem = m_manager->GetWorldActionSystem();
		if (!worldActionSystem || !worldActionSystem->SubmitPhysicalWorldJob(targetWorldKey, [netId, userId, e, requestId](ServerPhysicalWorld& physicalWorld)
			{
				physicalWorld.DespawnActorAsync(netId, userId, [e, requestId](bool ok)
					{
						SendDespawnActorResponse(e, ok, requestId);
					});
			}, [e, requestId]()
			{
				SendDespawnActorResponse(e, false, requestId);
			}))
		{
			SendDespawnActorResponse(e, false, requestId);
		}
	}

	void ServerUdpSession::OnPossessActorRequest(entt::entity e, const fb::fbPossessActorReq& req, uint32 requestId)
	{
		const WorldKey targetWorldKey = ResolveRequestedWorldKey(m_userId, req.world_id());
		if (!m_manager || !targetWorldKey.IsIssued())
		{
			SendPossessActorResponse(e, false, NetId::MakeRaw(req.net_id()), requestId);
			return;
		}

		const UserId userId = m_userId;
		const NetId  netId  = NetId::MakeRaw(req.net_id());

		auto* worldActionSystem = m_manager->GetWorldActionSystem();
		if (!worldActionSystem || !worldActionSystem->SubmitPhysicalWorldJob(targetWorldKey, [netId, userId, e, requestId](ServerPhysicalWorld& physicalWorld)
			{
				physicalWorld.PossessActorAsync(netId, userId, [e, requestId, netId](bool ok)
					{
						SendPossessActorResponse(e, ok, netId, requestId);
					});
			}, [e, netId, requestId]()
			{
				SendPossessActorResponse(e, false, netId, requestId);
			}))
		{
			SendPossessActorResponse(e, false, netId, requestId);
		}
	}

	void ServerUdpSession::OnUnpossessActorRequest(entt::entity e, const fb::fbUnpossessActorReq& req, uint32 requestId)
	{
		const WorldKey targetWorldKey = ResolveRequestedWorldKey(m_userId, req.world_id());
		if (!m_manager || !targetWorldKey.IsIssued())
		{
			SendUnpossessActorResponse(e, false, requestId);
			return;
		}

		const UserId userId = m_userId;
		const NetId  netId  = NetId::MakeRaw(req.net_id());

		auto* worldActionSystem = m_manager->GetWorldActionSystem();
		if (!worldActionSystem || !worldActionSystem->SubmitPhysicalWorldJob(targetWorldKey, [netId, userId, e, requestId](ServerPhysicalWorld& physicalWorld)
			{
				physicalWorld.UnpossessActorAsync(netId, userId, [e, requestId](bool ok)
					{
						SendUnpossessActorResponse(e, ok, requestId);
					});
			}, [e, requestId]()
			{
				SendUnpossessActorResponse(e, false, requestId);
			}))
		{
			SendUnpossessActorResponse(e, false, requestId);
		}
	}
}
