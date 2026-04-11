#include "pch.h"
#include "jamnet/runtime/ServerSession.h"
#include "jamnet/runtime/ServerNetworkManager.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/sync/networld/ServerNetWorld.h"
#include "jamnet/sync/replication/ReplicationUtils.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"

namespace jam::net
{
	namespace
	{
		static uint64 GetCallerPrincipalId(entt::entity sessionEntity)
		{
			auto& L = SHARD_LOCAL_CHECKED();
			auto& R = L.registry;

			if (!R.valid(sessionEntity))
				return 0;

			const auto* auth = R.try_get<SessionAuth>(sessionEntity);
			if (!auth || !auth->authenticated)
				return 0;

			return auth->principalId;
		}

		static WorldKey BuildTargetWorldKey(const fb::fbRequestWorldAssignmentReqT& req)
		{
			return WorldKey
			{
				.kind		= static_cast<eWorldKind>(req.target_world_kind),
				.templateId = req.target_world_template_id,
				.instanceId = req.target_world_instance_id,
			};
		}

		static fb::fbRequestWorldAssignmentResT BuildTransferAssignmentRes(
			uint8 requestAction,
			uint8 assignmentAction,
			const WorldTransferResult& transfer)
		{
			fb::fbRequestWorldAssignmentResT res{};
			res.request_action = requestAction;
			res.assignment_action = assignmentAction;
			res.reason = static_cast<uint8>(transfer.reason);

			if (transfer.Succeeded())
			{
				res.status = static_cast<uint8>(eWorldAssignmentStatus::Assigned);
				res.world_id = transfer.targetWorldId;
			}
			else
			{
				res.status = static_cast<uint8>(eWorldAssignmentStatus::Failed);
				res.world_id = INVALID_WORLD_ID;
			}

			return res;
		}

		static void SendWorldAssignmentNotification(ServerUdpSession& session, const fb::fbRequestWorldAssignmentResT& res)
		{
			flatbuffers::FlatBufferBuilder fbb(128);
			const auto root = fb::CreatefbRequestWorldAssignmentRes(fbb, &res);
			fbb.Finish(root);

			auto buf = PacketBuilder::CreateCustomPacket(
				CustomPacketId::WORLD_ASSIGNMENT,
				PacketFlags::NONE,
				eChannelType::RELIABLE_ORDERED,
				fbb.GetBufferPointer(),
				fbb.GetSize());

			if (buf)
				session.Send(buf);
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

	void ServerTcpSession::HandleCustomPacket(const PacketView& view)
	{
		if (!m_manager)
			return;

		if (auto* world = m_manager->GetWorld(m_worldId))
		{
			world->OnRecvPacket(m_userId, view);
		}
	}


	void ServerTcpSession::OnTcpBindRequest(entt::entity e, const fb::fbTcpBindReqT& req, uint32 requestId)
	{
		fb::fbTcpBindResT res{};

		if (req.user_id == 0)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::TCP_DEFAULT);
			return;
		}

		if (auto existing = m_manager->FindTcpSession(req.user_id); existing && existing.get() != this)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::TCP_DEFAULT);
			return;
		}

		m_userId = req.user_id;
		m_manager->RegisterTcpSession(m_userId, static_pointer_cast<ServerTcpSession>(shared_from_this()));

		{
			auto& L = SHARD_LOCAL_CHECKED();
			auto& R = L.registry;
			if (R.valid(e))
				R.emplace_or_replace<SessionAuth>(e, SessionAuth{ .principalId = m_userId, .authenticated = true });
		}

		res.user_id = req.user_id;
		res.success = true;

		RPCSendResponse(e, res, requestId, eChannelType::TCP_DEFAULT);
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
	}

	void ServerUdpSession::HandleCustomPacket(const PacketView& view)
	{
		if (!m_manager)
			return;

		if (auto* world = m_manager->GetWorld(m_worldId))
		{
			world->OnRecvPacket(m_userId, view);
		}
	}

	void ServerUdpSession::OnUdpBindRequest(entt::entity e, const fb::fbUdpBindReqT& req, uint32 requestId)
	{
		fb::fbUdpBindResT res{};

		if (req.user_id == 0)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		if (e != GetEntity())
		{
			JAMNET_LOG_ERROR("UdpBind RPC called on wrong entity! Expected={}, Got={}, SessionId={}", static_cast<uint32>(GetEntity()), static_cast<uint32>(e), GetSessionId());
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		if (m_userId != 0 && m_userId != req.user_id)
		{
			JAMNET_LOG_ERROR("UDP Session already bound to userId={}, cannot bind to userId={}", m_userId, req.user_id);
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		auto tcp = m_manager->FindTcpSession(req.user_id);
		if (!tcp || !tcp->IsConnected())
		{
			JAMNET_LOG_ERROR("UDP bind rejected for userId={} because TCP bind is missing", req.user_id);
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		if (auto existing = m_manager->FindUdpSession(req.user_id); existing && existing.get() != this)
		{
			JAMNET_LOG_ERROR("UDP Session already exists for userId={}", req.user_id);
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		m_userId = req.user_id;
		m_manager->RegisterUdpSession(m_userId, static_pointer_cast<ServerUdpSession>(shared_from_this()));

		{
			auto& L = SHARD_LOCAL_CHECKED();
			auto& R = L.registry;
			if (R.valid(e))
				R.emplace_or_replace<SessionAuth>(e, SessionAuth{ .principalId = m_userId, .authenticated = true });
		}

		res.user_id = req.user_id;
		res.success = true;

		RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
	}

	void ServerUdpSession::OnRequestWorldAssignmentReq(entt::entity e, const fb::fbRequestWorldAssignmentReqT& req, uint32 requestId)
	{
		fb::fbRequestWorldAssignmentResT res{};
		res.request_action    = static_cast<uint8>(req.action);
		res.assignment_action = static_cast<uint8>(eWorldAssignmentAction::None);
		res.reason            = static_cast<uint8>(eWorldTransferReason::None);

		if (!m_manager)
		{
			res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
			res.world_id = INVALID_WORLD_ID;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		if (m_userId == 0)
		{
			res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
			res.world_id = INVALID_WORLD_ID;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		if (req.action == fb::fbWorldRequestAction_Leave)
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
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		const WorldKey requestedWorld = BuildTargetWorldKey(req);
		WorldId targetWorldId = req.target_world_id;

		if (req.action == fb::fbWorldRequestAction_Join || req.action == fb::fbWorldRequestAction_Transfer)
		{
			res.assignment_action = (req.action == fb::fbWorldRequestAction_Transfer)
				? static_cast<uint8>(eWorldAssignmentAction::Transfer)
				: static_cast<uint8>(eWorldAssignmentAction::Join);

			if (targetWorldId == INVALID_WORLD_ID && requestedWorld.IsValid())
				targetWorldId = m_manager->ResolveOrAllocateWorldId(requestedWorld, {});

			if (targetWorldId == INVALID_WORLD_ID)
			{
				res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
				res.reason	 = static_cast<uint8>(eWorldTransferReason::InvalidArgument);
				res.world_id = INVALID_WORLD_ID;
				RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
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
					RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
					return;
				}

				ServerNetWorld* targetWorld = m_manager->GetOrCreateWorld(targetWorldId, targetOptions);
				if (!targetWorld)
				{
					res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
					res.reason	 = static_cast<uint8>(eWorldTransferReason::TargetUnavailable);
					res.world_id = INVALID_WORLD_ID;
					RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
					return;
				}

				m_manager->JoinWorld(targetWorldId, m_userId);
				if (!targetWorld->Enter(m_userId))
				{
					m_manager->LeaveWorld(targetWorldId, m_userId);
					res.status	 = static_cast<uint8>(eWorldAssignmentStatus::Failed);
					res.reason	 = static_cast<uint8>(eWorldTransferReason::MailboxClosed);
					res.world_id = INVALID_WORLD_ID;
					RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
					return;
				}

				m_worldId = targetWorldId;
				res.status   = static_cast<uint8>(eWorldAssignmentStatus::Assigned);
				res.world_id = m_worldId;
				
				RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
				return;
			}

			auto self = static_pointer_cast<ServerUdpSession>(shared_from_this());
			const uint8 requestAction = res.request_action;
			const uint8 assignmentAction = res.assignment_action;

			if (m_worldId == targetWorldId)
			{
				res.status = static_cast<uint8>(eWorldAssignmentStatus::Assigned);
				res.reason = static_cast<uint8>(eWorldTransferReason::AlreadyInTarget);
				res.world_id = m_worldId;
				RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
				return;
			}

			if (!m_manager->TransferWorldAsync(m_worldId, targetWorldId, m_userId,
				[self, targetWorldId, requestAction, assignmentAction](WorldTransferResult transfer) mutable
				{
					self->Post(Job([self, targetWorldId, requestAction, assignmentAction, transfer]() mutable
						{
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
				RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
				return;
			}

			res.status = static_cast<uint8>(eWorldAssignmentStatus::Waiting);
			res.world_id = INVALID_WORLD_ID;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
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
			auto self = static_pointer_cast<ServerUdpSession>(shared_from_this());
			const uint8 requestAction = res.request_action;
			const uint8 assignmentAction = res.assignment_action;
			if (!m_manager->AttachTransferCallback(m_userId,
				[self, requestAction, assignmentAction](WorldTransferResult transfer) mutable
				{
					self->Post(Job([self, requestAction, assignmentAction, transfer]() mutable
						{
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

		RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
	}

	void ServerUdpSession::OnSpawnActorRequest(entt::entity e, const fb::fbSpawnActorReqT& req, uint32 requestId)
	{
		fb::fbSpawnActorResT res{};

		if (!m_manager || m_worldId == INVALID_WORLD_ID)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_worldId);
		if (!nw)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		const px::PrefabKey key{ req.prefab_key };
		if (!key.IsValid() || !req.pos || !req.rot)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		const uint64 userId = GetCallerPrincipalId(e);
		if (userId == 0)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		if ((req.owner_user_id != 0 && req.owner_user_id != userId)
			|| (req.controller_user_id != 0 && req.controller_user_id != userId))
		{
			JAMNET_LOG_ERROR("Spawn request rejected due to principal mismatch. principal={}, owner={}, controller={}",
				userId, req.owner_user_id, req.controller_user_id);
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		SpawnParams params{};

		params.spawnId		= req.spawn_req_id;
		params.owner		= (req.owner_user_id != 0 || req.controller_user_id != 0) ? userId : 0;
		params.controller	= (req.controller_user_id != 0) ? userId : 0;
		params.targetNetId  = NetId::MakeRaw(req.target_net_id);
		params.desc.prefab	= key;
		params.desc.pose    = { .p = { req.pos->x(), req.pos->y(), req.pos->z() }, .q = { req.rot->x(), req.rot->y(), req.rot->z(), req.rot->w() } };
		params.desc.team	= static_cast<uint16>(req.team_id);
		params.desc.part	= static_cast<uint8>(req.part_id);
		params.desc.role	= static_cast<uint8>(req.role_id);

		px::SpawnOverrideMask::Flag overrideMask{ req.override_mask };

		if (px::IsRigidOverrideMask(overrideMask))
		{
			px::RigidSpawnOverrides overrides{};
			overrides.mask = overrideMask;

			if (overrideMask.has_any(px::SpawnOverrideMask::LINEAR_VEL) && req.linear_vel)
				overrides.linearVelocity = px::Vec3{ req.linear_vel->x(), req.linear_vel->y(), req.linear_vel->z() };
			if (overrideMask.has_any(px::SpawnOverrideMask::ANGULAR_VEL) && req.angular_vel)
				overrides.angularVelocity = px::Vec3{ req.angular_vel->x(), req.angular_vel->y(), req.angular_vel->z() };
			if (overrideMask.has_any(px::SpawnOverrideMask::LINEAR_DAMP))
				overrides.linearDamping = req.linear_damping;
			if (overrideMask.has_any(px::SpawnOverrideMask::ANGULAR_DAMP))
				overrides.angularDamping = req.angular_damping;

			params.desc.overrides = overrides;
		}
		else
		{
			px::CharacterSpawnOverrides overrides{};
			overrides.mask = overrideMask;

			if (overrideMask.has_any(px::SpawnOverrideMask::VIEW_YAW))
				overrides.yaw = req.yaw;
			if (overrideMask.has_any(px::SpawnOverrideMask::VIEW_PITCH))
				overrides.pitch = req.pitch;

			params.desc.overrides = overrides;
		}

		const auto self = static_pointer_cast<ServerUdpSession>(shared_from_this());
		const uint32 reqId		= requestId;
		const uint32 spawnReqId = req.spawn_req_id;

		nw->SpawnActorAsync(params, [self, reqId, spawnReqId](NetId netId) mutable
			{
				fb::fbSpawnActorResT asyncRes{};
				asyncRes.success	  = netId.IsValid();
				asyncRes.net_id		  = netId.Raw();
				asyncRes.spawn_req_id = spawnReqId;

				self->Post(Job([self, asyncRes = asyncRes, reqId]() mutable
					{
						RPCSendResponse(self->GetEntity(), asyncRes, reqId, eChannelType::RELIABLE_ORDERED);
					}));
			});
	}

	void ServerUdpSession::OnDespawnActorRequest(entt::entity e, const fb::fbDespawnActorReqT& req, uint32 requestId)
	{
		fb::fbDespawnActorResT res{};

		if (!m_manager || m_worldId == INVALID_WORLD_ID)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_worldId);
		if (!nw)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		const uint64 userId = GetCallerPrincipalId(e);
		const NetId  netId = NetId::MakeRaw(req.net_id);

		nw->DespawnActorAsync(netId, userId, [e, requestId](bool ok)
			{
				fb::fbDespawnActorResT asyncRes{};
				asyncRes.success = ok;

				RPCSendResponse(e, asyncRes, requestId, eChannelType::RELIABLE_ORDERED);
			});
	}

	void ServerUdpSession::OnPossessActorRequest(entt::entity e, const fb::fbPossessActorReqT& req, uint32 requestId)
	{
		fb::fbPossessActorResT res{};

		if (!m_manager || m_worldId == INVALID_WORLD_ID)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_worldId);
		if (!nw)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		const uint64 userId = GetCallerPrincipalId(e);
		const NetId  netId = NetId::MakeRaw(req.net_id);

		nw->PossessActorAsync(netId, userId, [e, requestId, netId = req.net_id](bool ok)
			{
				fb::fbPossessActorResT asyncRes{};
				asyncRes.success = ok;
				asyncRes.net_id  = netId;

				RPCSendResponse(e, asyncRes, requestId, eChannelType::RELIABLE_ORDERED);
			});
	}

	void ServerUdpSession::OnUnpossessActorRequest(entt::entity e, const fb::fbUnpossessActorReqT& req, uint32 requestId)
	{
		fb::fbUnpossessActorResT res{};

		if (!m_manager || m_worldId == INVALID_WORLD_ID)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_worldId);
		if (!nw)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		const uint64 userId = GetCallerPrincipalId(e);
		const NetId  netId  = NetId::MakeRaw(req.net_id);

		nw->UnpossessActorAsync(netId, userId, [e, requestId](bool ok)
			{
				fb::fbUnpossessActorResT asyncRes{};
				asyncRes.success = ok;

				RPCSendResponse(e, asyncRes, requestId, eChannelType::RELIABLE_ORDERED);
			});
	}
}
