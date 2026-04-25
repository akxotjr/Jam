#include "pch.h"
#include "jamnet/runtime/ServerSession.h"

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/RPC.h"

#include "jamnet/sync/networld/ServerNetWorld.h"
#include "jamnet/sync/replication/ReplicationCodec.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"

#include "jamnet/runtime/ServerNetworkManager.h"

namespace jam::net
{
	namespace
	{
		uint64 GetCallerPrincipalId(entt::entity sessionEntity)
		{
			auto& L = CurrentShardLocalChecked();
			auto& R = L.registry;

			if (!R.valid(sessionEntity))
				return 0;

			const auto* auth = R.try_get<SessionAuth>(sessionEntity);
			if (!auth || !auth->authenticated)
				return 0;

			return auth->principalId;
		}

		WorldKey BuildTargetWorldKey(const fb::fbRequestWorldAssignmentReq& req)
		{
			return WorldKey
			{
				.kind		= static_cast<eWorldKind>(req.target_world_kind()),
				.templateId = req.target_world_template_id(),
				.instanceId = req.target_world_instance_id(),
			};
		}

		struct WorldAssignmentResponse
		{
			uint8 status = static_cast<uint8>(eWorldAssignmentStatus::Failed);
			uint8 request_action = 0;
			uint8 assignment_action = 0;
			uint8 reason = 0;
			WorldId world_id = INVALID_WORLD_ID;
		};

		WorldAssignmentResponse BuildTransferAssignmentRes(
			uint8 requestAction,
			uint8 assignmentAction,
			const WorldTransferResult& transfer)
		{
			return WorldAssignmentResponse
			{
				.status = static_cast<uint8>(transfer.Succeeded() ? eWorldAssignmentStatus::Assigned : eWorldAssignmentStatus::Failed),
				.request_action = requestAction,
				.assignment_action = assignmentAction,
				.reason = static_cast<uint8>(transfer.reason),
				.world_id = transfer.Succeeded() ? transfer.targetWorldId : INVALID_WORLD_ID,
			};
		}

		void BuildWorldAssignmentFlatBuffer(flatbuffers::FlatBufferBuilder& fbb, const WorldAssignmentResponse& res)
		{
			const auto root = fb::CreatefbRequestWorldAssignmentRes(
				fbb,
				res.status,
				res.request_action,
				res.assignment_action,
				res.reason,
				res.world_id);
			fbb.Finish(root);
		}

		void SendWorldAssignmentResponse(entt::entity e, const WorldAssignmentResponse& res, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(128);
			BuildWorldAssignmentFlatBuffer(fbb, res);
			RPCSendResponse<fb::fbRequestWorldAssignmentRes>(e, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendWorldAssignmentNotification(ServerUdpSession& session, const WorldAssignmentResponse& res)
		{
			flatbuffers::FlatBufferBuilder fbb(128);
			BuildWorldAssignmentFlatBuffer(fbb, res);

			auto buf = PacketBuilder::CreateCustomPacket(
				CustomPacketId::WORLD_ASSIGNMENT,
				PacketFlags::NONE,
				eChannel::RELIABLE_ORDERED,
				fbb.GetBufferPointer(),
				fbb.GetSize());

			if (buf.IsValid())
				session.Send(buf);
		}

		void SendTcpBindResponse(entt::entity e, bool success, uint64 userId, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbTcpBindRes(fbb, success, userId);
			fbb.Finish(root);
			RPCSendResponse<fb::fbTcpBindRes>(e, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), requestId, eChannel::TCP_DEFAULT);
		}

		void SendUdpBindResponse(entt::entity e, bool success, uint64 userId, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbUdpBindRes(fbb, success, userId);
			fbb.Finish(root);
			RPCSendResponse<fb::fbUdpBindRes>(e, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendSpawnActorResponse(entt::entity e, bool success, uint64 spawnReqId, NetId netId, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbSpawnActorRes(fbb, success, spawnReqId, netId.Raw());
			fbb.Finish(root);
			RPCSendResponse<fb::fbSpawnActorRes>(e, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendDespawnActorResponse(entt::entity e, bool success, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbDespawnActorRes(fbb, success);
			fbb.Finish(root);
			RPCSendResponse<fb::fbDespawnActorRes>(e, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendPossessActorResponse(entt::entity e, bool success, NetId netId, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbPossessActorRes(fbb, success, netId.Raw());
			fbb.Finish(root);
			RPCSendResponse<fb::fbPossessActorRes>(e, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), requestId, eChannel::RELIABLE_ORDERED);
		}

		void SendUnpossessActorResponse(entt::entity e, bool success, uint32 requestId)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbUnpossessActorRes(fbb, success);
			fbb.Finish(root);
			RPCSendResponse<fb::fbUnpossessActorRes>(e, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), requestId, eChannel::RELIABLE_ORDERED);
		}
	}


	void ServerTcpSession::OnConnected()
	{
		JAMNET_LOG_INFO("ServerTcpSession connected", GetRemoteNetAddress().GetIpAddress(), GetRemoteNetAddress().GetPort());

		if (!m_manager)
		{
			JAMNET_LOG_ERROR_LOC("NetworkManager not set");
			Disconnect();
		}
	}

	void ServerTcpSession::OnDisconnected()
	{
		JAMNET_LOG_INFO("[UserId = {}] ServerTcpSession disconnected", m_userId);

		if (m_manager && m_userId != 0)
		{
			m_manager->UnregisterSession(m_userId);
		}
	}

	void ServerTcpSession::HandleCustomPacket(const PacketHeaderView& view)
	{
		if (!m_manager)
			return;

		if (auto* world = m_manager->GetWorld(m_worldId))
		{
			world->OnRecvPacket(m_userId, view);
		}
	}

	void ServerTcpSession::OnEntityCreated(entt::registry& R, entt::entity e)
	{
		RPCRegisterRequest<fb::fbTcpBindReq>(R, e, this, &ServerTcpSession::OnTcpBindRequest);
	}


	void ServerTcpSession::OnTcpBindRequest(entt::entity e, const fb::fbTcpBindReq& req, uint32 requestId)
	{
		if (req.user_id() == 0)
		{
			SendTcpBindResponse(e, false, 0, requestId);
			return;
		}

		if (auto existing = m_manager->FindTcpSession(req.user_id()); existing && existing != this)
		{
			SendTcpBindResponse(e, false, 0, requestId);
			return;
		}

		m_userId = req.user_id();
		m_manager->RegisterTcpSession(m_userId, this);

		{
			auto& L = CurrentShardLocalChecked();
			auto& R = L.registry;
			if (R.valid(e))
				R.emplace_or_replace<SessionAuth>(e, SessionAuth{ .principalId = m_userId, .authenticated = true });
		}

		SendTcpBindResponse(e, true, req.user_id(), requestId);
	}

	void ServerUdpSession::OnConnected()
	{
		JAMNET_LOG_INFO("ServerUdpSession connected from {}:{}", GetRemoteNetAddress().GetIpAddress(), GetRemoteNetAddress().GetPort());

		if (!m_manager)
		{
			JAMNET_LOG_ERROR_LOC("NetworkManager not set");
			Disconnect();
		}
	}

	void ServerUdpSession::OnDisconnected()
	{
		JAMNET_LOG_INFO("[UserId = {}] ServerUdpSession disconnected", m_userId);

		if (m_manager && m_userId != 0)
			m_manager->UnregisterUdpSession(m_userId, this);
	}

	void ServerUdpSession::HandleCustomPacket(const PacketHeaderView& view)
	{
		if (!m_manager)
			return;

		if (auto* world = m_manager->GetWorld(m_worldId))
		{
			world->OnRecvPacket(m_userId, view);
		}
	}

	void ServerUdpSession::OnEntityCreated(entt::registry& R, entt::entity e)
	{
		RPCRegisterRequest<fb::fbUdpBindReq>(R, e, this, &ServerUdpSession::OnUdpBindRequest);
		RPCRegisterRequest<fb::fbRequestWorldAssignmentReq>(R, e, this, &ServerUdpSession::OnRequestWorldAssignmentReq);
		RPCRegisterRequest<fb::fbSpawnActorReq>(R, e, this, &ServerUdpSession::OnSpawnActorRequest);
		RPCRegisterRequest<fb::fbDespawnActorReq>(R, e, this, &ServerUdpSession::OnDespawnActorRequest);
		RPCRegisterRequest<fb::fbPossessActorReq>(R, e, this, &ServerUdpSession::OnPossessActorRequest);
		RPCRegisterRequest<fb::fbUnpossessActorReq>(R, e, this, &ServerUdpSession::OnUnpossessActorRequest);
	}

	void ServerUdpSession::OnUdpBindRequest(entt::entity e, const fb::fbUdpBindReq& req, uint32 requestId)
	{
		if (req.user_id() == 0)
		{
			SendUdpBindResponse(e, false, 0, requestId);
			return;
		}

		if (e != GetEntity())
		{
			JAMNET_LOG_ERROR("UdpBind RPC called on wrong entity! Expected={}, Got={}, SessionId={}", static_cast<uint32>(GetEntity()), static_cast<uint32>(e), GetSessionId());
			SendUdpBindResponse(e, false, 0, requestId);
			return;
		}

		if (m_userId != 0 && m_userId != req.user_id())
		{
			JAMNET_LOG_ERROR("UDP Session already bound to userId={}, cannot bind to userId={}", m_userId, req.user_id());
			SendUdpBindResponse(e, false, 0, requestId);
			return;
		}

		auto tcp = m_manager->FindTcpSession(req.user_id());
		if (!tcp || !tcp->IsConnected())
		{
			JAMNET_LOG_ERROR("UDP bind rejected for userId={} because TCP bind is missing", req.user_id());
			SendUdpBindResponse(e, false, 0, requestId);
			return;
		}

		if (auto existing = m_manager->FindUdpSession(req.user_id()); existing && existing != this)
		{
			JAMNET_LOG_ERROR("UDP Session already exists for userId={}", req.user_id());
			SendUdpBindResponse(e, false, 0, requestId);
			return;
		}

		m_userId = req.user_id();
		m_manager->RegisterUdpSession(m_userId, this);

		{
			auto& L = CurrentShardLocalChecked();
			auto& R = L.registry;
			if (R.valid(e))
				R.emplace_or_replace<SessionAuth>(e, SessionAuth{ .principalId = m_userId, .authenticated = true });
		}

		SendUdpBindResponse(e, true, req.user_id(), requestId);
	}

	void ServerUdpSession::OnRequestWorldAssignmentReq(entt::entity e, const fb::fbRequestWorldAssignmentReq& req, uint32 requestId)
	{
		WorldAssignmentResponse res{};
		res.request_action    = static_cast<uint8>(req.action());
		res.assignment_action = static_cast<uint8>(eWorldAssignmentAction::None);
		res.reason            = static_cast<uint8>(eWorldTransferReason::None);

		if (!m_manager)
		{
			res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
			res.world_id = INVALID_WORLD_ID;
			SendWorldAssignmentResponse(e, res, requestId);
			return;
		}

		if (m_userId == 0)
		{
			res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
			res.world_id = INVALID_WORLD_ID;
			SendWorldAssignmentResponse(e, res, requestId);
			return;
		}

		if (req.action() == fb::fbWorldRequestAction_Leave)
		{
			if (m_worldId != INVALID_WORLD_ID)
			{
				if (auto* world = m_manager->GetWorld(m_worldId))
					world->Leave(m_userId);

				m_manager->LeaveWorld(m_worldId, m_userId);
				m_worldId = INVALID_WORLD_ID;
			}

			res.status   = static_cast<uint8>(eWorldAssignmentStatus::Assigned);
			res.world_id = INVALID_WORLD_ID;
			SendWorldAssignmentResponse(e, res, requestId);
			return;
		}

		const WorldKey requestedWorld = BuildTargetWorldKey(req);
		WorldId targetWorldId = req.target_world_id();

		if (req.action() == fb::fbWorldRequestAction_Join || req.action() == fb::fbWorldRequestAction_Transfer)
		{
			res.assignment_action = (req.action() == fb::fbWorldRequestAction_Transfer)
				? static_cast<uint8>(eWorldAssignmentAction::Transfer)
				: static_cast<uint8>(eWorldAssignmentAction::Join);

			if (targetWorldId == INVALID_WORLD_ID && requestedWorld.IsValid())
				targetWorldId = m_manager->ResolveOrAllocateWorldId(requestedWorld, {});

			if (targetWorldId == INVALID_WORLD_ID)
			{
				res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
				res.reason	 = static_cast<uint8>(eWorldTransferReason::InvalidArgument);
				res.world_id = INVALID_WORLD_ID;
				SendWorldAssignmentResponse(e, res, requestId);
				return;
			}

			if (m_worldId == INVALID_WORLD_ID)
			{
				const WorldOptions targetOptions = m_manager->GetWorldOptions(targetWorldId);
				if (targetOptions.capacity != 0 && m_manager->GetWorldMemberCount(targetWorldId) >= targetOptions.capacity)
				{
					res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
					res.reason	 = static_cast<uint8>(eWorldTransferReason::CapacityExceeded);
					res.world_id = INVALID_WORLD_ID;
					SendWorldAssignmentResponse(e, res, requestId);
					return;
				}

				ServerNetWorld* targetWorld = m_manager->GetOrCreateWorld(targetWorldId, targetOptions);
				if (!targetWorld)
				{
					res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
					res.reason	 = static_cast<uint8>(eWorldTransferReason::TargetUnavailable);
					res.world_id = INVALID_WORLD_ID;
					SendWorldAssignmentResponse(e, res, requestId);
					return;
				}

				m_manager->JoinWorld(targetWorldId, m_userId);
				if (!targetWorld->Enter(m_userId))
				{
					m_manager->LeaveWorld(targetWorldId, m_userId);
					res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
					res.reason	 = static_cast<uint8>(eWorldTransferReason::MailboxClosed);
					res.world_id = INVALID_WORLD_ID;
					SendWorldAssignmentResponse(e, res, requestId);
					return;
				}

				m_worldId = targetWorldId;
				res.status   = static_cast<uint8>(eWorldAssignmentStatus::Assigned);
				res.world_id = m_worldId;
				
				SendWorldAssignmentResponse(e, res, requestId);
				return;
			}

			const SessionHandle handle = GetSessionHandle();
			const uint8 requestAction = res.request_action;
			const uint8 assignmentAction = res.assignment_action;

			if (m_worldId == targetWorldId)
			{
				res.status = static_cast<uint8>(eWorldAssignmentStatus::Assigned);
				res.reason = static_cast<uint8>(eWorldTransferReason::AlreadyInTarget);
				res.world_id = m_worldId;
				SendWorldAssignmentResponse(e, res, requestId);
				return;
			}

			if (!m_manager->TransferWorldAsync(m_worldId, targetWorldId, m_userId,
				[handle, targetWorldId, requestAction, assignmentAction](WorldTransferResult transfer) mutable
				{
					const auto shard = GLOBAL_EXEC.GetShard(handle.routeKey);
					if (!shard)
						return;

					shard->Submit(Job([handle, targetWorldId, requestAction, assignmentAction, transfer]() mutable
						{
							auto& L = CurrentShardLocalChecked();
							auto* self = static_cast<ServerUdpSession*>(FindSessionByHandle(L, handle));
							if (!self)
								return;

							if (transfer.Succeeded())
								self->m_worldId = targetWorldId;

							const auto asyncRes = BuildTransferAssignmentRes(requestAction, assignmentAction, transfer);
							SendWorldAssignmentNotification(*self, asyncRes);
						}));
				}))
			{
				res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
				res.reason	 = static_cast<uint8>(eWorldTransferReason::ConflictingTransfer);
				res.world_id = INVALID_WORLD_ID;
				SendWorldAssignmentResponse(e, res, requestId);
				return;
			}

			res.status = static_cast<uint8>(eWorldAssignmentStatus::Waiting);
			res.world_id = INVALID_WORLD_ID;
			SendWorldAssignmentResponse(e, res, requestId);
			return;
		}

		const WorldAssignmentRequest assignmentReq
		{
			.principalId	 = m_userId,
			.currentWorldId = m_worldId,
			.currentWorld	 = m_manager->GetWorldKey(m_worldId)
		};

		const WorldAssignmentResult assignment = m_manager->RequestWorldAssignment(assignmentReq);

		res.status	 = static_cast<uint8>(assignment.status);
		res.assignment_action = static_cast<uint8>(assignment.action);
		res.world_id = assignment.IsAssigned() ? assignment.worldId : INVALID_WORLD_ID;

		if (assignment.IsAssigned())
			m_worldId = assignment.worldId;
		else if (assignment.status == eWorldAssignmentStatus::Waiting)
		{
			const SessionHandle handle = GetSessionHandle();
			const uint8 requestAction = res.request_action;
			const uint8 assignmentAction = res.assignment_action;
			if (!m_manager->AttachTransferCallback(m_userId,
				[handle, requestAction, assignmentAction](WorldTransferResult transfer) mutable
				{
					const auto shard = GLOBAL_EXEC.GetShard(handle.routeKey);
					if (!shard)
						return;

					shard->Submit(Job([handle, requestAction, assignmentAction, transfer]() mutable
						{
							auto& L = CurrentShardLocalChecked();
							auto* self = static_cast<ServerUdpSession*>(FindSessionByHandle(L, handle));
							if (!self)
								return;

							if (transfer.Succeeded())
								self->m_worldId = transfer.targetWorldId;

							const auto asyncRes = BuildTransferAssignmentRes(requestAction, assignmentAction, transfer);
							SendWorldAssignmentNotification(*self, asyncRes);
						}));
				}))
			{
				res.status = static_cast<uint8>(eWorldAssignmentStatus::Failed);
				res.assignment_action = static_cast<uint8>(eWorldAssignmentAction::Reject);
				res.reason = static_cast<uint8>(eWorldTransferReason::ConflictingTransfer);
			}
		}

		SendWorldAssignmentResponse(e, res, requestId);
	}

	void ServerUdpSession::OnSpawnActorRequest(entt::entity e, const fb::fbSpawnActorReq& req, uint32 requestId)
	{
		if (!m_manager || m_worldId == INVALID_WORLD_ID)
		{
			SendSpawnActorResponse(e, false, req.spawn_req_id(), NetId::Invalid(), requestId);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_worldId);
		if (!nw)
		{
			SendSpawnActorResponse(e, false, req.spawn_req_id(), NetId::Invalid(), requestId);
			return;
		}

		const px::PrefabKey key{ req.prefab_key() };
		if (!key.IsValid() || !req.pos() || !req.rot())
		{
			SendSpawnActorResponse(e, false, req.spawn_req_id(), NetId::Invalid(), requestId);
			return;
		}

		const uint64 userId = GetCallerPrincipalId(e);
		if (userId == 0)
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

		params.spawnId		= req.spawn_req_id();
		params.owner		= (req.owner_user_id() != 0 || req.controller_user_id() != 0) ? userId : 0;
		params.controller	= (req.controller_user_id() != 0) ? userId : 0;
		params.targetNetId  = NetId::MakeRaw(req.target_net_id());
		params.desc.spawnSrc = px::eSpawnSource::Runtime;
		params.desc.prefab	= key;
		params.desc.pose    = { .p = { req.pos()->x(), req.pos()->y(), req.pos()->z() }, .q = { req.rot()->x(), req.rot()->y(), req.rot()->z(), req.rot()->w() } };
		params.desc.team	= static_cast<uint16>(req.team_id());
		params.desc.part	= static_cast<uint8>(req.part_id());
		params.desc.role	= static_cast<uint8>(req.role_id());

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

		const SessionHandle handle = GetSessionHandle();
		const uint32 reqId		= requestId;
		const uint32 spawnReqId = req.spawn_req_id();

		nw->SpawnActorAsync(params, [handle, reqId, spawnReqId](NetId netId) mutable
			{
				const auto shard = GLOBAL_EXEC.GetShard(handle.routeKey);
				if (!shard)
					return;

				shard->Submit(Job([handle, netId, spawnReqId, reqId]() mutable
					{
						auto& L = CurrentShardLocalChecked();
						auto* self = static_cast<ServerUdpSession*>(FindSessionByHandle(L, handle));
						if (!self)
							return;

						SendSpawnActorResponse(self->GetEntity(), netId.IsValid(), spawnReqId, netId, reqId);
					}));
			});
	}

	void ServerUdpSession::OnDespawnActorRequest(entt::entity e, const fb::fbDespawnActorReq& req, uint32 requestId)
	{
		if (!m_manager || m_worldId == INVALID_WORLD_ID)
		{
			SendDespawnActorResponse(e, false, requestId);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_worldId);
		if (!nw)
		{
			SendDespawnActorResponse(e, false, requestId);
			return;
		}

		const uint64 userId = GetCallerPrincipalId(e);
		const NetId  netId = NetId::MakeRaw(req.net_id());

		nw->DespawnActorAsync(netId, userId, [e, requestId](bool ok)
			{
				SendDespawnActorResponse(e, ok, requestId);
			});
	}

	void ServerUdpSession::OnPossessActorRequest(entt::entity e, const fb::fbPossessActorReq& req, uint32 requestId)
	{
		if (!m_manager || m_worldId == INVALID_WORLD_ID)
		{
			SendPossessActorResponse(e, false, NetId::MakeRaw(req.net_id()), requestId);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_worldId);
		if (!nw)
		{
			SendPossessActorResponse(e, false, NetId::MakeRaw(req.net_id()), requestId);
			return;
		}

		const uint64 userId = GetCallerPrincipalId(e);
		const NetId  netId = NetId::MakeRaw(req.net_id());

		nw->PossessActorAsync(netId, userId, [e, requestId, netId = req.net_id()](bool ok)
			{
				SendPossessActorResponse(e, ok, NetId::MakeRaw(netId), requestId);
			});
	}

	void ServerUdpSession::OnUnpossessActorRequest(entt::entity e, const fb::fbUnpossessActorReq& req, uint32 requestId)
	{
		if (!m_manager || m_worldId == INVALID_WORLD_ID)
		{
			SendUnpossessActorResponse(e, false, requestId);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_worldId);
		if (!nw)
		{
			SendUnpossessActorResponse(e, false, requestId);
			return;
		}

		const uint64 userId = GetCallerPrincipalId(e);
		const NetId  netId  = NetId::MakeRaw(req.net_id());

		nw->UnpossessActorAsync(netId, userId, [e, requestId](bool ok)
			{
				SendUnpossessActorResponse(e, ok, requestId);
			});
	}
}
