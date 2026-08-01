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
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/protocol/schema/gen/social_command_generated.h"
#include "jamnet/runtime/application/ServerNetworkManager.h"
#include "jamnet/runtime/world/lifecycle/ServerWorldTransitionCoordinator.h"

namespace jam::net
{
	namespace
	{

		uint16 ResolveAccountShardIndex(AccountId accountId)
		{
			if (accountId == kInvalidAccountId)
				return static_cast<uint16>(kInvalidRouteShard);

			if (auto shard = GLOBAL_EXEC.GetAffinityShard(accountId))
				return static_cast<uint16>(shard->GetIndex());

			return static_cast<uint16>(kInvalidRouteShard);
		}


		void SendSpawnActorResponse(entt::entity e, fb::fbSpawnActorFailure failure, ClientRequestId clientRequestId, ActorId actorId, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const bool success = failure == fb::fbSpawnActorFailure_None && actorId.IsValid();
			const auto root = fb::CreatefbSpawnActorRes(fbb, success, failure, clientRequestId, actorId.Value());
			fbb.Finish(root);
			RPCSendResponse<fb::fbSpawnActorRes>(e, fbb.GetBufferPointer(), fbb.GetSize(), requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendSpawnPlayerResponse(entt::entity e, fb::fbSpawnPlayerFailure failure, ClientRequestId clientRequestId, ActorId actorId, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const bool success = failure == fb::fbSpawnPlayerFailure_None && actorId.IsValid();
			const auto root = fb::CreatefbSpawnPlayerRes(fbb, success, failure, clientRequestId, actorId.Value());
			fbb.Finish(root);
			RPCSendResponse<fb::fbSpawnPlayerRes>(e, fbb.GetBufferPointer(), fbb.GetSize(), requestId, eChannel::TCP_DEFAULT);
		}

		void SendDespawnActorResponse(entt::entity e, bool success, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbDespawnActorRes(fbb, success);
			fbb.Finish(root);
			RPCSendResponse<fb::fbDespawnActorRes>(e, fbb.GetBufferPointer(), fbb.GetSize(), requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendDespawnPlayerResponse(entt::entity e, bool success, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbDespawnPlayerRes(fbb, success);
			fbb.Finish(root);
			RPCSendResponse<fb::fbDespawnPlayerRes>(e, fbb.GetBufferPointer(), fbb.GetSize(), requestId, eChannel::TCP_DEFAULT);
		}

		std::optional<WorldRuntimeRef> ResolveMainRuntime(const UserContext& ctx, WorldId worldId)
		{
			return ctx.physicalWorld.main && ctx.physicalWorld.main->worldId == worldId
				? ctx.physicalWorld.main : std::nullopt;
		}

		std::optional<WorldRuntimeRef> ResolveRequestedMainRuntime(UserId userId, WorldId worldId)
		{
			if (userId == kInvalidUserId || worldId == kInvalidWorldId)
				return {};

			auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
			if (const auto* ctx = state.FindUserContext(userId))
				return ResolveMainRuntime(*ctx, worldId);
			return std::nullopt;
		}

		fb::fbSpawnActorFailure ToWireSpawnFailure(ePlayerSpawnFailure failure)
		{
			switch (failure)
			{
			case ePlayerSpawnFailure::None: return fb::fbSpawnActorFailure_None;
			case ePlayerSpawnFailure::InvalidCorrelation: return fb::fbSpawnActorFailure_InvalidCorrelation;
			case ePlayerSpawnFailure::AlreadySpawned: return fb::fbSpawnActorFailure_AlreadySpawned;
			case ePlayerSpawnFailure::SpawnFailed: return fb::fbSpawnActorFailure_SpawnFailed;
			}
			return fb::fbSpawnActorFailure_SpawnFailed;
		}

		fb::fbSpawnPlayerFailure ToWireSpawnPlayerFailure(ePlayerSpawnFailure failure)
		{
			switch (failure)
			{
			case ePlayerSpawnFailure::None: return fb::fbSpawnPlayerFailure_None;
			case ePlayerSpawnFailure::InvalidCorrelation: return fb::fbSpawnPlayerFailure_InvalidCorrelation;
			case ePlayerSpawnFailure::AlreadySpawned: return fb::fbSpawnPlayerFailure_AlreadySpawned;
			case ePlayerSpawnFailure::SpawnFailed: return fb::fbSpawnPlayerFailure_SpawnFailed;
			}
			return fb::fbSpawnPlayerFailure_SpawnFailed;
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
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			const auto* wire = flatbuffers::GetRoot<fb::fbEnterWorldRequest>(view.Payload());
			if (!wire || !wire->Verify(verifier))
				return;
			EnterWorldRequest request
			{
				.requestId = wire->request_id(),
				.archetypeKey = WorldArchetypeKey{ wire->archetype_key() },
				.selector = static_cast<eWorldDestinationSelector>(wire->selector()),
				.explicitInstanceId = WorldInstanceId{ wire->explicit_instance_id() },
				.destinationName = wire->destination_name() ? wire->destination_name()->str() : std::string{},
				.expectedMainRevision = wire->expected_main_revision(),
			};
			m_manager->EnterWorld(m_userId, request);
			return;
		}
		if (view.Id() == CustomPacketId::LEAVE_WORLD_REQUEST && m_userId != kInvalidUserId)
		{
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			const auto* wire = flatbuffers::GetRoot<fb::fbLeaveWorldRequest>(view.Payload());
			if (!wire || !wire->Verify(verifier))
				return;
			const LeaveWorldRequest request
			{
				.requestId = wire->request_id(),
				.expectedMainRevision = wire->expected_main_revision(),
			};
			m_manager->LeaveWorld(m_userId, request);
			return;
		}
		if (view.Id() == CustomPacketId::CLIENT_BARRIER_RESULT && m_userId != kInvalidUserId)
		{
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			const auto* result = flatbuffers::GetRoot<fb::fbClientBarrierResult>(view.Payload());
			if (!result || !result->Verify(verifier))
				return;
			if (auto* coordinator = m_manager->GetWorldTransitionSystem())
				coordinator->OnClientBarrierResult(m_userId,
					{ .token = { result->barrier_token() }, .succeeded = result->succeeded(),
					  .failure = static_cast<eWorldTransitionFailure>(result->failure()) }, NOW_NS());
			return;
		}
		if (view.Id() == CustomPacketId::SOCIAL_COMMAND && m_userId != kInvalidUserId)
		{
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			if (!fb::VerifyfbSocialCommandBuffer(verifier))
				return;

			const auto* wire = fb::GetfbSocialCommand(view.Payload());
			const auto* destination = wire ? wire->destination() : nullptr;
			const auto* payload = wire ? wire->payload() : nullptr;
			if (!wire || !destination || !payload
				|| destination->audience() < fb::fbSocialAudience_MIN
				|| destination->audience() > fb::fbSocialAudience_MAX)
				return;

			SocialCommand command{
				.requestId = wire->request_id(),
				.destination = {
					.audience = static_cast<eSocialAudience>(destination->audience()),
					.scopeId = destination->scope_id(),
				},
				.contentType = wire->content_type(),
			};
			command.payload.resize(payload->size());
			if (!command.payload.empty())
				std::memcpy(command.payload.data(), payload->data(), payload->size());
			m_manager->DispatchSocialCommand(m_userId, std::move(command));
			return;
		}

		const WorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidWorldId || m_userId == kInvalidUserId)
			return;

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		const auto* ctx = state.FindUserContext(m_userId);
		if (!ctx)
			return;

		const auto targetRuntime = ResolveMainRuntime(*ctx, worldId);
		if (!targetRuntime)
			return;
		m_manager->SubmitWorldJob(*targetRuntime, [userId = m_userId, packet = std::move(packet)](WorldBase& world) mutable
			{
				if (auto* host = dynamic_cast<WorldMembershipHost*>(&world))
				{
					host->HandleWorldPacket(userId, std::move(packet));
				}
			});
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
		auto& R = CurrentShardLocalChecked().registry;
		const entt::entity e = GetEntity();
		if (e == entt::null || !R.valid(e))
			return;

		RPCRegisterRequest<fb::fbSpawnPlayerReq>(R, e, this, &ServerTcpSession::OnSpawnPlayerRequest);
		RPCRegisterRequest<fb::fbDespawnPlayerReq>(R, e, this, &ServerTcpSession::OnDespawnPlayerRequest);
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
				if (ctx->tcp == kInvalidSessionId && ctx->udp == kInvalidSessionId && !ctx->physicalWorld.main)
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

		const WorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidWorldId || m_userId == kInvalidUserId)
			return;

		auto& state = GetOrCreateUserShardState(CurrentShardLocalChecked());
		const auto* ctx = state.FindUserContext(m_userId);
		if (!ctx)
			return;

		const auto targetRuntime = ResolveMainRuntime(*ctx, worldId);
		if (!targetRuntime)
			return;
		m_manager->SubmitWorldJob(*targetRuntime, [userId = m_userId, packet = std::move(packet)](WorldBase& world) mutable
			{
				if (auto* host = dynamic_cast<WorldMembershipHost*>(&world))
				{
					host->HandleWorldPacket(userId, std::move(packet));
				}
			});
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

		RPCRegisterRequest<fb::fbDespawnActorReq>(R, e, this, &ServerUdpSession::OnDespawnActorRequest);
		RPCRegisterRequest<fb::fbSpawnActorReq>(R, e, this, &ServerUdpSession::OnSpawnActorRequest);
	}


	void ServerTcpSession::OnSpawnPlayerRequest(entt::entity e, const fb::fbSpawnPlayerReq& req, uint32 requestId)
	{
		JAMNET_LOG_DEBUG("[ServerTcpSession::OnSpawnPlayerRequest] account id= {}, user id= {}", m_accountId, m_userId);

		if (!m_manager || m_userId == kInvalidUserId)
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_InvalidCorrelation, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		auto& userShardState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		const auto* userContext = userShardState.FindUserContext(m_userId);
		if (!userContext || userContext->physicalWorld.revision != req.expected_main_revision())
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_StaleRevision, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		const auto& main = userContext->physicalWorld.main;
		if (!main || main->worldId != req.world_id()
			|| main->instance.instanceId != WorldInstanceId{ req.world_instance_id() })
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_InvalidCorrelation, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}
		const WorldRuntimeRef targetRuntime = *main;
		const WorldEventCorrelation correlation{ .world = targetRuntime, .mainRevision = req.expected_main_revision() };

		const ActorArchetypeKey actorArchetypeKey = ActorArchetypeKey::FromU64(req.actor_archetype_key());
		if (!IsValidAssetKey(actorArchetypeKey) || !req.pos() || !req.rot())
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_SpawnFailed, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		const UserId userId = m_userId;
		if (userId == kInvalidUserId)
		{
			SendSpawnPlayerResponse(e, fb::fbSpawnPlayerFailure_InvalidCorrelation, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		SpawnParams params{};

		params.clientRequestId		= req.client_request_id();
		params.actorArchetypeKey	= actorArchetypeKey;
		params.owner				= userId;
		params.controller			= userId;
		params.targetActorId			= ActorId(req.target_actor_id());
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
		const ClientRequestId clientRequestId = req.client_request_id();

		if (!m_manager->SubmitWorldJob(targetRuntime, [params, correlation, sessionId, e, reqId, clientRequestId, userId](WorldBase& world) mutable
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

								SendSpawnPlayerResponse(self->GetEntity(), ToWireSpawnPlayerFailure(failure), clientRequestId, actorId, reqId);
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
		if (!userContext || userContext->physicalWorld.revision != req.expected_main_revision()
			|| !userContext->physicalWorld.main)
		{
			SendDespawnPlayerResponse(e, false, requestId);
			return;
		}

		const WorldRuntimeRef runtime = *userContext->physicalWorld.main;
		if (runtime.worldId != req.world_id() || runtime.instance.instanceId != WorldInstanceId{ req.world_instance_id() })
		{
			SendDespawnPlayerResponse(e, false, requestId);
			return;
		}

		const SessionId sessionId = GetSessionId();
		const ActorId actorId = ActorId(req.actor_id());
		const WorldEventCorrelation correlation{ .world = runtime, .mainRevision = req.expected_main_revision() };
		if (!m_manager->SubmitWorldJob(runtime, [sessionId, e, requestId, actorId, userId = m_userId, correlation](WorldBase& world)
			{
				auto* physicalWorld = dynamic_cast<ServerWorld*>(&world);
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

		const auto targetRuntime = ResolveRequestedMainRuntime(m_userId, req.world_id());
		if (!targetRuntime || targetRuntime->instance.instanceId != WorldInstanceId{ req.world_instance_id() })
		{
			SendSpawnActorResponse(e, fb::fbSpawnActorFailure_InvalidCorrelation, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		const ActorArchetypeKey archetype = ActorArchetypeKey::FromU64(req.actor_archetype_key());
		if (!IsValidAssetKey(archetype) || !req.pos() || !req.rot()
			|| (req.owner_user_id() != 0 && req.owner_user_id() != m_userId)
			|| req.controller_user_id() != 0)
		{
			SendSpawnActorResponse(e, fb::fbSpawnActorFailure_SpawnFailed, req.client_request_id(), ActorId::Invalid(), requestId);
			return;
		}

		SpawnParams params{};
		params.clientRequestId = req.client_request_id();
		params.actorArchetypeKey = archetype;
		params.owner = req.owner_user_id() != 0 ? m_userId : 0;
		params.targetActorId = ActorId(req.target_actor_id());
		params.desc.spawnSrc = px::eSpawnSource::Runtime;
		params.desc.pose = { .p = { req.pos()->x(), req.pos()->y(), req.pos()->z() }, .q = { req.rot()->x(), req.rot()->y(), req.rot()->z(), req.rot()->w() } };
		params.desc.team = static_cast<uint16>(req.team_id());
		params.desc.part = static_cast<uint8>(req.part_id());
		params.desc.role = static_cast<uint8>(req.role_id());

		px::SpawnOverrideMask::Flag overrideMask{ req.override_mask() };
		if (px::IsRigidOverrideMask(overrideMask))
		{
			px::RigidSpawnOverrides overrides{};
			overrides.mask = overrideMask;
			if (overrideMask.has_any(px::SpawnOverrideMask::LINEAR_VEL) && req.linear_vel()) overrides.linearVelocity = { req.linear_vel()->x(), req.linear_vel()->y(), req.linear_vel()->z() };
			if (overrideMask.has_any(px::SpawnOverrideMask::ANGULAR_VEL) && req.angular_vel()) overrides.angularVelocity = { req.angular_vel()->x(), req.angular_vel()->y(), req.angular_vel()->z() };
			if (overrideMask.has_any(px::SpawnOverrideMask::LINEAR_DAMP)) overrides.linearDamping = req.linear_damping();
			if (overrideMask.has_any(px::SpawnOverrideMask::ANGULAR_DAMP)) overrides.angularDamping = req.angular_damping();
			params.desc.overrides = overrides;
		}
		else
		{
			px::CharacterSpawnOverrides overrides{};
			overrides.mask = overrideMask;
			if (overrideMask.has_any(px::SpawnOverrideMask::VIEW_YAW)) overrides.yaw = req.yaw();
			if (overrideMask.has_any(px::SpawnOverrideMask::VIEW_PITCH)) overrides.pitch = req.pitch();
			params.desc.overrides = overrides;
		}

		const SessionId sessionId = GetSessionId();
		const ClientRequestId clientRequestId = req.client_request_id();
		if (!m_manager->SubmitWorldJob(*targetRuntime, [params, sessionId, e, requestId, clientRequestId](WorldBase& world) mutable
			{
				auto* physicalWorld = dynamic_cast<ServerWorld*>(&world);
				if (!physicalWorld) return;
				const ActorId actorId = physicalWorld->SpawnActor(std::move(params));
				if (auto shard = GLOBAL_EXEC.GetShardFromIndex(GetRuntimeShardIndex(sessionId)))
					shard->Submit(Job([sessionId, e, requestId, clientRequestId, actorId]()
					{
						auto* self = static_cast<ServerUdpSession*>(GetOrCreateSessionShardState().FindSession(sessionId));
						if (self && self->GetEntity() == e)
							SendSpawnActorResponse(e, actorId.IsValid() ? fb::fbSpawnActorFailure_None : fb::fbSpawnActorFailure_SpawnFailed, clientRequestId, actorId, requestId);
					}));
			}))
		{
			SendSpawnActorResponse(e, fb::fbSpawnActorFailure_SpawnFailed, req.client_request_id(), ActorId::Invalid(), requestId);
		}
	}

	void ServerUdpSession::OnDespawnActorRequest(entt::entity e, const fb::fbDespawnActorReq& req, uint32 requestId)
	{
		const auto targetRuntime = ResolveRequestedMainRuntime(m_userId, req.world_id());
		if (!m_manager || !targetRuntime || targetRuntime->instance.instanceId != WorldInstanceId{ req.world_instance_id() })
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

		if (!m_manager->SubmitWorldJob(*targetRuntime, [actorId, userId, sessionId, e, requestId](WorldBase& world)
			{
				auto* physicalWorld = dynamic_cast<ServerWorld*>(&world);
				if (!physicalWorld) return;
				const bool ok = physicalWorld->DespawnActor(actorId, userId);
				if (auto shard = GLOBAL_EXEC.GetShardFromIndex(GetRuntimeShardIndex(sessionId)))
					shard->Submit(Job([sessionId, e, requestId, ok]()
					{
						auto* self = static_cast<ServerUdpSession*>(GetOrCreateSessionShardState().FindSession(sessionId));
						if (self && self->GetEntity() == e)
							SendDespawnActorResponse(e, ok, requestId);
					}));
			}))
		{
			SendDespawnActorResponse(e, false, requestId);
		}
	}

}
