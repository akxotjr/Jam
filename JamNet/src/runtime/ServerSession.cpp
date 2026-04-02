#include "pch.h"
#include "jamnet/runtime/ServerSession.h"
#include "jamnet/runtime/ServerNetworkManager.h"
#include "jamnet/sync/networld/ServerNetWorld.h"
#include "jamnet/sync/replication/ReplicationUtils.h"

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

		if (auto* world = m_manager->GetWorld(m_groupId))
		{
			world->OnRecvPacket(view);
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

		if (auto* world = m_manager->GetWorld(m_groupId))
		{
			world->OnRecvPacket(view);
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

	void ServerUdpSession::OnRequestGroupIdReq(entt::entity e, const fb::fbRequestGroupIdReqT& req, uint32 requestId)
	{
		fb::fbRequestGroupIdResT res{};

		if (!m_manager)
		{
			res.status	 = static_cast<uint8>(eMatchmakeStatus::Failed);
			res.group_id = 0;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		if (m_userId == 0)
		{
			res.status	 = static_cast<uint8>(eMatchmakeStatus::Failed);
			res.group_id = 0;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		auto* mm = m_manager->GetMatchmaker();
		if (!mm)
		{
			res.status	 = static_cast<uint8>(eMatchmakeStatus::Failed);
			res.group_id = 0;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		const MatchmakeRequest mreq{ .principalId = m_userId };
		const MatchmakeResult  mres = mm->RequestGroupId(mreq);

		res.status	 = static_cast<uint8>(mres.status);
		res.group_id = (mres.status == eMatchmakeStatus::Assigned) ? mres.groupId : 0;

		if (mres.status == eMatchmakeStatus::Assigned && mres.groupId != 0)
		{
			auto* world = m_manager->GetOrCreateWorld(mres.groupId);
			if (!world)
			{
				res.status	 = static_cast<uint8>(eMatchmakeStatus::Failed);
				res.group_id = 0;
				RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
				return;
			}

			m_groupId = mres.groupId;
			m_manager->JoinGroup(m_groupId, m_userId);
			world->Enter(m_userId);
		}

		RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
	}

	void ServerUdpSession::OnSpawnActorRequest(entt::entity e, const fb::fbSpawnActorReqT& req, uint32 requestId)
	{
		fb::fbSpawnActorResT res{};

		if (!m_manager || m_groupId == 0)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_groupId);
		if (!nw)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		const px::PrefabKey key{ req.prefab_key };
		if (!key.IsValid())
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		SpawnParams params{};

		params.spawnId		= req.spawn_req_id;
		params.owner		= req.owner_user_id;
		params.controller	= req.controller_user_id;
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

		if (!m_manager || m_groupId == 0)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_groupId);
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

		if (!m_manager || m_groupId == 0)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_groupId);
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

		if (!m_manager || m_groupId == 0)
		{
			res.success = false;
			RPCSendResponse(e, res, requestId, eChannelType::RELIABLE_ORDERED);
			return;
		}

		ServerNetWorld* nw = m_manager->GetWorld(m_groupId);
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
