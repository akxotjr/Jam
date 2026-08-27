#include "pch.h"
#include "jamnet/runtime/session/ServerSession.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/net/RPC.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/utils/Clock.h"

#include "jamnet/runtime/world/simulation/server/ServerWorld.h"
#include "jamnet/runtime/protocol/codec/ReplicationCodec.h"
#include "jamnet/runtime/protocol/codec/WorldCodec.h"
#include "jamnet/runtime/protocol/codec/SocialCodec.h"
#include "jamnet/runtime/protocol/codec/ActorCodec.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/protocol/schema/gen/social_command_generated.h"
#include "jamnet/runtime/protocol/codec/ContentCodec.h"
#include "jamnet/runtime/application/ServerNetworkManager.h"
#include "jamnet/runtime/session/RuntimeShardRouting.h"
#include "jamnet/runtime/world/lifecycle/ServerWorldTransitionCoordinator.h"

namespace jam::net
{
	namespace
	{
		void SendSpawnActorResponse(entt::entity e, fb::fbSpawnActorFailure failure, ClientRequestId clientRequestId, ActorId actorId, uint32 requestId)
		{
			const bool success = failure == fb::fbSpawnActorFailure_None && actorId.IsValid();
			const auto payload = codec::EncodeSpawnActorResponse(success, failure, clientRequestId, actorId);
			RPCSendResponse<fb::fbSpawnActorRes>(e, payload.data, payload.size, requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendSpawnPlayerResponse(entt::entity e, fb::fbSpawnPlayerFailure failure, ClientRequestId clientRequestId, ActorId actorId, uint32 requestId)
		{
			const bool success = failure == fb::fbSpawnPlayerFailure_None && actorId.IsValid();
			const auto payload = codec::EncodeSpawnPlayerResponse(success, failure, clientRequestId, actorId);
			RPCSendResponse<fb::fbSpawnPlayerRes>(e, payload.data, payload.size, requestId, eChannel::TCP_DEFAULT);
		}

		void SendDespawnActorResponse(entt::entity e, bool success, uint32 requestId)
		{
			const auto payload = codec::EncodeDespawnActorResponse(success);
			RPCSendResponse<fb::fbDespawnActorRes>(e, payload.data, payload.size, requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendDespawnPlayerResponse(entt::entity e, bool success, uint32 requestId)
		{
			const auto payload = codec::EncodeDespawnPlayerResponse(success);
			RPCSendResponse<fb::fbDespawnPlayerRes>(e, payload.data, payload.size, requestId, eChannel::TCP_DEFAULT);
		}

		std::optional<WorldRef> ResolveMainWorld(const UserContext& ctx, WorldId worldId)
		{
			return ctx.worldState.main && ctx.worldState.main->worldId == worldId
				? ctx.worldState.main : std::nullopt;
		}

		std::optional<WorldRef> ResolveRequestedMainWorld(UserId userId, WorldId worldId)
		{
			if (userId == kInvalidUserId || worldId == kInvalidWorldId)
				return {};

			auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
			if (const auto* ctx = state.FindUserContext(userId))
				return ResolveMainWorld(*ctx, worldId);
			return std::nullopt;
		}

	}



	ServerSessionBundle ResolveUserSessionBundle(const UserContext& user)
	{
		JAM_ASSERT(CurrentShardLocal() && CurrentShardLocal()->shardIndex == GetUserShardIndex(user.userId));

		auto& sessionState = GetOrCreateSessionShardState(CurrentShardLocalChecked());

		ServerSessionBundle bundle;

		if (user.tcp != kInvalidSessionId)
		{
			if (auto* session = sessionState.FindSession(user.tcp))
				bundle.tcp.Set(static_cast<ServerTcpSession*>(session));
		}

		if (user.udp != kInvalidSessionId)
		{
			if (auto* session = sessionState.FindSession(user.udp))
				bundle.udp.Set(static_cast<ServerUdpSession*>(session));
		}

		return bundle;
	}




	void ServerTcpSession::OnSessionEstablished()
	{
		JAM_LOG_INFO("[AccountId = {}, UserId = {}] ServerTcpSession established. ip: {} | port: {}", 
			GetAccountId(), 
			GetUserId(), 
			GetRemoteNetAddress().GetIpAddress(), 
			GetRemoteNetAddress().GetPort());

		AttachToUserContext();
		if (IsClosing())
			return;
		BootstrapRPC();
	}

	void ServerTcpSession::OnSessionReleased()
	{
		JAM_LOG_INFO("[AccountId = {}, UserId = {}] ServerTcpSession disconnected", GetAccountId(), GetUserId());

		if (m_manager && m_userId != kInvalidUserId)
			m_manager->ReleaseSession(m_userId, this);
	}

	void ServerTcpSession::HandleCustomPacket(Packet packet)
	{
		if (!m_manager)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		if (view.Id() == CustomPacketId::ENTER_WORLD_REQUEST && m_userId != kInvalidUserId)
		{
			EnterWorldRequest request;
			if (!codec::DecodeEnterWorldRequest(view.Payload(), view.PayloadSize(), request))
				return;

			m_manager->EnterWorld(m_userId, request);
			return;
		}

		if (view.Id() == CustomPacketId::LEAVE_WORLD_REQUEST && m_userId != kInvalidUserId)
		{
			LeaveWorldRequest request;
			if (!codec::DecodeLeaveWorldRequest(view.Payload(), view.PayloadSize(), request))
				return;

			m_manager->LeaveWorld(m_userId, request);
			return;
		}

		if (view.Id() == CustomPacketId::CLIENT_WORLD_SYNC_RESULT && m_userId != kInvalidUserId)
		{
			ClientWorldSyncResult result;
			if (!codec::DecodeClientWorldSyncResult(view.Payload(), view.PayloadSize(), result))
				return;

			if (auto* coordinator = m_manager->GetWorldTransitionSystem())
				coordinator->OnClientWorldSyncResult(m_userId, result, NOW_NS());

			return;
		}

		if (view.Id() == CustomPacketId::SOCIAL_COMMAND && m_userId != kInvalidUserId)
		{
			SocialCommand command;
			if (!codec::DecodeSocialCommand(view.Payload(), view.PayloadSize(), command))
				return;
			m_manager->DispatchSocialCommand(m_userId, std::move(command));
			return;
		}

		if (view.Id() == CustomPacketId::CONTENT && m_userId != kInvalidUserId)
		{
			GenericContentRequest request;
			if (!codec::DecodeContentRequest(view.Payload(), view.PayloadSize(), request))
				return;
			const ClientRequestId requestId = request.requestId;
			const GenericContentOperationCode opCode = request.opCode;
			if (!m_manager->DispatchContentRequest(m_userId, std::move(request)))
			{
				Send(codec::MakeContentResponsePacket({
					.requestId = requestId,
					.opCode = opCode,
					.status = eGenericContentResponseStatus::Unavailable,
				}));
			}
			return;
		}

		const WorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidWorldId || m_userId == kInvalidUserId)
			return;

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		const auto* ctx = state.FindUserContext(m_userId);
		if (!ctx)
			return;

		const auto targetWorld = ResolveMainWorld(*ctx, worldId);
		if (!targetWorld)
			return;

		SubmitWorldJob(*targetWorld, [userId = m_userId, packet = std::move(packet)](WorldBase& world) mutable
			{
				if (auto* host = dynamic_cast<WorldMembershipHost*>(&world))
				{
					host->HandleWorldPacket(userId, std::move(packet));
				}
			});
	}

	void ServerTcpSession::AttachToUserContext()
	{
		if (!m_manager || m_accountId == kInvalidAccountId || m_userId == kInvalidUserId || GetSessionId() == kInvalidSessionId)
		{
			Disconnect();
			return;
		}

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* ctx = state.FindUserContext(m_userId);

		if (!ctx || ctx->accountId != m_accountId || ctx->connectionState == eUserConnectionState::Released)
		{
			Disconnect();
			return;
		}

		auto& sessionState = GetOrCreateSessionShardState(CurrentShardLocalChecked());
		if (ctx->tcp != kInvalidSessionId && ctx->tcp != GetSessionId() && sessionState.FindSessionRef(ctx->tcp).TryGet())
		{
			JAM_LOG_WARN("TCP session is already registered.");
			Disconnect();
			return;
		}

		ctx->tcp = GetSessionId();
		m_manager->NotifyUserConnected(*ctx);
		if (auto* transitions = m_manager->GetWorldTransitionSystem())
			transitions->OnReconnected(m_userId);
	}

	void ServerTcpSession::BootstrapRPC()
	{
		auto& R = CurrentShardLocalChecked().registry;
		const entt::entity e = GetEntity();
		if (e == entt::null || !R.valid(e))
			return;

		RPCRegisterRequest<fb::fbSpawnPlayerReq>(R, e, this, &ServerTcpSession::OnSpawnPlayerRequest);
		RPCRegisterRequest<fb::fbDespawnPlayerReq>(R, e, this, &ServerTcpSession::OnDespawnPlayerRequest);
	}



	void ServerUdpSession::OnSessionEstablished()
	{
		JAM_LOG_INFO("[AccountId = {}, UserId = {}] ServerUdpSession established. ip: {} | port: {}",
			GetAccountId(),
			GetUserId(),
			GetRemoteNetAddress().GetIpAddress(),
			GetRemoteNetAddress().GetPort());

		AttachToUserContext();
		if (IsClosing())
			return;
		BootstrapRPC();
	}

	void ServerUdpSession::OnSessionReleased()
	{
		JAM_LOG_INFO("[AccountId= {}, UserId = {}] ServerUdpSession disconnected", GetAccountId(), GetUserId());

		if (m_userId != kInvalidUserId)
		{
			auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
			if (auto* ctx = state.FindUserContext(m_userId))
			{
				if (ctx->udp == GetSessionId())
					ctx->udp = kInvalidSessionId;
				if (ctx->connectionState == eUserConnectionState::Released && ctx->tcp == kInvalidSessionId && ctx->udp == kInvalidSessionId && !ctx->worldState.main)
					state.FreeUserContext(m_userId);
			}

			if (m_manager)
				if (auto* transitions = m_manager->GetWorldTransitionSystem())
					transitions->OnSessionChanged(m_userId);
		}
	}

	void ServerUdpSession::HandleCustomPacket(Packet packet)
	{
		if (!m_manager)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		const WorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidWorldId || m_userId == kInvalidUserId)
			return;

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		const auto* ctx = state.FindUserContext(m_userId);
		if (!ctx)
			return;

		const auto targetWorld = ResolveMainWorld(*ctx, worldId);
		if (!targetWorld)
			return;

		SubmitWorldJob(*targetWorld, [userId = m_userId, packet = std::move(packet)](WorldBase& world) mutable
			{
				if (auto* host = dynamic_cast<WorldMembershipHost*>(&world))
				{
					host->HandleWorldPacket(userId, std::move(packet));
				}
			});
	}

	void ServerUdpSession::AttachToUserContext()
	{
		if (!m_manager || m_accountId == kInvalidAccountId || m_userId == kInvalidUserId || GetSessionId() == kInvalidSessionId)
		{
			Disconnect();
			return;
		}

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* ctx = state.FindUserContext(m_userId);
		if (!ctx || ctx->accountId != m_accountId || ctx->connectionState == eUserConnectionState::Released || ctx->tcp == kInvalidSessionId)
		{
			Disconnect();
			return;
		}

		auto& sessionState = GetOrCreateSessionShardState(CurrentShardLocalChecked());
		if (ctx->udp != kInvalidSessionId && ctx->udp != GetSessionId() && sessionState.FindSessionRef(ctx->udp).TryGet())
		{
			JAM_LOG_WARN("UDP session is already registered.");
			Disconnect();
			return;
		}

		ctx->udp = GetSessionId();
		if (auto* transitions = m_manager->GetWorldTransitionSystem())
			transitions->OnSessionChanged(m_userId);
	}

	void ServerUdpSession::BootstrapRPC()
	{
		auto& R = CurrentShardLocalChecked().registry;
		const entt::entity e = GetEntity();
		if (e == entt::null || !R.valid(e))
			return;

		RPCRegisterRequest<fb::fbDespawnActorReq>(R, e, this, &ServerUdpSession::OnDespawnActorRequest);
		RPCRegisterRequest<fb::fbSpawnActorReq>(R, e, this, &ServerUdpSession::OnSpawnActorRequest);
	}


	void ServerTcpSession::OnSpawnPlayerRequest(entt::entity e, const fb::fbSpawnPlayerReq& req, uint32 requestId)
	{
		if (!m_manager || m_userId == kInvalidUserId)
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_InvalidCorrelation, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		auto& userShardState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		const auto* userContext = userShardState.FindUserContext(m_userId);
		if (!userContext || userContext->worldState.revision != req.expected_main_revision())
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_StaleRevision, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		const auto& main = userContext->worldState.main;
		if (!main || main->worldId != req.world_id()
			|| main->instance.instanceId != WorldInstanceId{ req.world_instance_id() })
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_InvalidCorrelation, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}
		const WorldRef targetWorld = *main;
		const WorldEventCorrelation correlation{ .world = targetWorld, .mainRevision = req.expected_main_revision() };

		SpawnParams params{};
		if (!codec::DecodeSpawnPlayerRequest(req, m_userId, params)
			|| !IsValidAssetKey(params.actorArchetypeKey))
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_SpawnFailed, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}
		params.desc.spawnSrc = px::eSpawnSource::Runtime;

		const UserId userId = m_userId;
		if (userId == kInvalidUserId)
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_InvalidCorrelation, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		const SessionId sessionId = GetSessionId();
		const uint32 reqId		= requestId;
		const ClientRequestId clientRequestId = req.client_request_id();

		if (SubmitWorldJob(targetWorld, [params, correlation, sessionId, reqId, clientRequestId, userId](WorldBase& world) mutable
			{
				auto* physicalWorld = dynamic_cast<ServerWorld*>(&world);
				if (!physicalWorld) return;
				physicalWorld->SpawnPlayerAsync(userId, correlation, params, [sessionId, reqId, clientRequestId](ActorId actorId, ePlayerSpawnFailure failure) mutable
					{
						const auto shard = GLOBAL_EXEC.GetShardFromIndex(GetRuntimeShardIndex(sessionId));
						if (!shard)
							return;

						shard->Submit(Job([sessionId, actorId, clientRequestId, reqId, failure]() mutable
							{
								auto& state = GetOrCreateSessionShardState();
								auto* self = static_cast<ServerTcpSession*>(state.FindSession(sessionId));
								if (!self)
									return;

								SendSpawnPlayerResponse(self->GetEntity(), codec::EncodeSpawnPlayerFailure(failure), clientRequestId, actorId, reqId);
							}));
					});
			}))
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_SpawnFailed, req.client_request_id(), ActorId::Invalid(), requestId);
		}
	}

	void ServerTcpSession::OnDespawnPlayerRequest(entt::entity e, const fb::fbDespawnPlayerReq& req, uint32 requestId)
	{
		if (!m_manager || m_userId == kInvalidUserId || req.actor_id() == 0)
		{
			SendDespawnPlayerResponse(e, false, requestId);
			return;
		}

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		const auto* userContext = state.FindUserContext(m_userId);
		if (!userContext || userContext->worldState.revision != req.expected_main_revision()
			|| !userContext->worldState.main)
		{
			SendDespawnPlayerResponse(e, false, requestId);
			return;
		}

		const WorldRef world = *userContext->worldState.main;
		if (world.worldId != req.world_id() || world.instance.instanceId != WorldInstanceId{ req.world_instance_id() })
		{
			SendDespawnPlayerResponse(e, false, requestId);
			return;
		}

		const SessionId sessionId = GetSessionId();
		const ActorId actorId = ActorId(req.actor_id());
		const WorldEventCorrelation correlation{ .world = world, .mainRevision = req.expected_main_revision() };
		if (!SubmitWorldJob(world, [sessionId, e, requestId, actorId, userId = m_userId, correlation](WorldBase& target)
			{
				auto* physicalWorld = dynamic_cast<ServerWorld*>(&target);
				if (!physicalWorld) return;
				const bool ok = physicalWorld->DespawnPlayer(userId, correlation, actorId);
				if (auto shard = GLOBAL_EXEC.GetShardFromIndex(GetRuntimeShardIndex(sessionId)))
					shard->Submit(Job([sessionId, e, requestId, ok]()
					{
						auto* self = static_cast<ServerTcpSession*>(GetOrCreateSessionShardState().FindSession(sessionId));
						if (self && self->GetEntity() == e)
							SendDespawnPlayerResponse(e, ok, requestId);
					}));
			}))
		{
			SendDespawnPlayerResponse(e, false, requestId);
		}
	}

	void ServerUdpSession::OnSpawnActorRequest(entt::entity e, const fb::fbSpawnActorReq& req, uint32 requestId)
	{
		if (!m_manager || m_userId == kInvalidUserId)
		{
			SendSpawnActorResponse(e, fb::fbSpawnActorFailure_InvalidCorrelation, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		const auto targetWorld = ResolveRequestedMainWorld(m_userId, req.world_id());
		if (!targetWorld || targetWorld->instance.instanceId != WorldInstanceId{ req.world_instance_id() })
		{
			SendSpawnActorResponse(e, fb::fbSpawnActorFailure_InvalidCorrelation, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		SpawnParams params{};
		if (!codec::DecodeSpawnActorRequest(req, params)
			|| !IsValidAssetKey(params.actorArchetypeKey)
			|| (params.owner != 0 && params.owner != m_userId)
			|| params.controller != 0)
		{
			SendSpawnActorResponse(e, fb::fbSpawnActorFailure_SpawnFailed, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}
		params.owner = params.owner != 0 ? m_userId : 0;
		params.desc.spawnSrc = px::eSpawnSource::Runtime;

		const SessionId sessionId = GetSessionId();
		const ClientRequestId clientRequestId = req.client_request_id();
		if (SubmitWorldJob(*targetWorld, [params, sessionId, e, requestId, clientRequestId](WorldBase& world) mutable
			{
				auto* physicalWorld = dynamic_cast<ServerWorld*>(&world);
				if (!physicalWorld) return;
				physicalWorld->SpawnActorAsync(std::move(params), [sessionId, e, requestId, clientRequestId](ActorId actorId)
					{
						if (auto shard = GLOBAL_EXEC.GetShardFromIndex(GetRuntimeShardIndex(sessionId)))
							shard->Submit(Job([sessionId, e, requestId, clientRequestId, actorId]()
								{
									auto* self = static_cast<ServerUdpSession*>(GetOrCreateSessionShardState().FindSession(sessionId));
									if (self && self->GetEntity() == e)
										SendSpawnActorResponse(e, actorId.IsValid() ? fb::fbSpawnActorFailure_None : fb::fbSpawnActorFailure_SpawnFailed, clientRequestId, actorId, requestId);
								}));
					});
			}))
		{
			SendSpawnActorResponse(e, fb::fbSpawnActorFailure_SpawnFailed, req.client_request_id(), ActorId::Invalid(), requestId);
		}
	}

	void ServerUdpSession::OnDespawnActorRequest(entt::entity e, const fb::fbDespawnActorReq& req, uint32 requestId)
	{
		const auto targetWorld = ResolveRequestedMainWorld(m_userId, req.world_id());
		if (!m_manager || !targetWorld || targetWorld->instance.instanceId != WorldInstanceId{ req.world_instance_id() })
		{
			SendDespawnActorResponse(e, false, requestId);
			return;
		}

		const UserId userId = m_userId;
		if (req.actor_id() == 0)
		{
			SendDespawnActorResponse(e, false, requestId);
			return;
		}

		const ActorId actorId = ActorId(req.actor_id());
		const SessionId sessionId = GetSessionId();

		if (!SubmitWorldJob(*targetWorld, [actorId, userId, sessionId, e, requestId](WorldBase& world)
			{
				auto* physicalWorld = dynamic_cast<ServerWorld*>(&world);
				if (!physicalWorld) return;
				physicalWorld->DespawnActorAsync(actorId, userId, [sessionId, e, requestId](bool ok)
					{
						if (auto shard = GLOBAL_EXEC.GetShardFromIndex(GetRuntimeShardIndex(sessionId)))
							shard->Submit(Job([sessionId, e, requestId, ok]()
								{
									auto* self = static_cast<ServerUdpSession*>(GetOrCreateSessionShardState().FindSession(sessionId));
									if (self && self->GetEntity() == e)
										SendDespawnActorResponse(e, ok, requestId);
								}));
					});
			}))
		{
			SendDespawnActorResponse(e, false, requestId);
		}
	}

}
