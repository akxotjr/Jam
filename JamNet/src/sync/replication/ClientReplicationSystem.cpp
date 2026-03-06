#include "pch.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationUtils.h"
#include "jamnet/sync/replication/ClientReplicationSystem.h"

#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/NetWorldContext.h"

namespace jam::net
{


	ClientReplicationSystem::ClientReplicationSystem(entt::registry& world)
		: m_world(world)
	{
	}

	void ClientReplicationSystem::Init()
	{
		Clear();

		if (auto* nw = m_world.ctx().find<ClientNetWorld*>())
			m_userId = (*nw) ? (*nw)->GetUserId() : 0;
	}

	void ClientReplicationSystem::Clear()
	{
		m_replicas.clear();
		m_localNetId	 = 0;
		m_lastServerTick = 0;
		m_lastInputAck	 = 0;
		//m_pendingServerState.reset();
	}

	void ClientReplicationSystem::Tick()
	{
		while (!m_pendingSnapshots.empty())
		{
			fb::fbSnapshotT snapshot = std::move(m_pendingSnapshots.front());
			m_pendingSnapshots.pop_front();

			const auto* hdr = snapshot.header.get();
			if (!hdr) continue;

			const uint64 serverTick = hdr->server_tick;
			m_lastInputAck	 = hdr->input_ack;
			m_lastServerTick = serverTick;

			for (const auto& entPtr : snapshot.entities)
			{
				if (!entPtr) continue;
				ProcessEntity(*entPtr, serverTick);
			}

			PruneOldReplicas(serverTick);
		}
	}





	void ClientReplicationSystem::EnqueueSnapshot(fb::fbSnapshotT snapshot)
	{
		m_pendingSnapshots.emplace_back(std::move(snapshot));
	}

	//optional<ServerState> ClientReplicationSystem::ConsumePendingServerState()
	//{
	//	if (!m_pendingServerState.has_value())
	//		return nullopt;
	//	auto state = m_pendingServerState;
	//	m_pendingServerState.reset();
	//	return state;
	//}


	void ClientReplicationSystem::ProcessEntity(const fb::fbActorEntityT& ent, uint64 serverTick)
	{
		const uint32 netId		 = ent.net_id;
		const uint32 baselineRev = ent.baseline_rev;

		const bool hasFull		= (ent.transform_full  != nullptr);
		const bool hasDelta		= (ent.transform_delta != nullptr);
		const bool hasCharFull	= (ent.character_full  != nullptr);
		const bool hasCharDelta = (ent.character_delta != nullptr);

		const entt::entity resolved = ResolveEntityForSnapshot(netId, ent.meta.get());
		if (resolved == entt::null || !m_world.valid(resolved))
			return;

		Replica& replica = GetOrCreateReplica(netId);
		replica.e			 = resolved;
		replica.lastSeenTick = serverTick;

		if (const auto* meta = ent.meta.get())
			UpdateUniqueLocalFromMeta(netId, *meta, replica);

		if (hasCharFull)
		{
			ApplyCharacterFullSnapshot(serverTick, netId, ent.character_full.get(), baselineRev, replica.isLocal);
		}
		else if (hasCharDelta)
		{
			ApplyCharacterDeltaSnapshot(serverTick, netId, ent.character_delta.get(), baselineRev, replica.isLocal);
		}
		else if (hasFull)
		{
			ApplyRigidFullSnapshot(serverTick, netId, ent.transform_full.get(), baselineRev);
		}
		else if (hasDelta)
		{
			ApplyRigidDeltaSnapshot(serverTick, netId, ent.transform_delta.get(), baselineRev);
		}

		if (m_world.all_of<RigidPhysicalBody>(resolved) || m_world.all_of<CharacterPhysicalBody>(resolved))
			return;

		if (auto* phys = m_world.ctx().find<ClientPhysicsSystem>())
			phys->SpawnActor(resolved, replica.isLocal);
	}

	entt::entity ClientReplicationSystem::ResolveEntityForSnapshot(uint32 netId, const fb::fbActorMetaT* meta)
	{
		entt::entity e = entt::null;

		auto* ctx = m_world.ctx().find<ClientNetWorld*>();
		if (!ctx || !*ctx) return e;

		ClientNetWorld* netWorld = *ctx;

		if (!meta) return netWorld->GetEntity(netId);

		const px::PrefabKey prefabKey{ meta->prefab_key };
		const uint32 spawnReqId = meta->spawn_req_id;
		const uint64 owner		= meta->owner_user_id;
		const uint64 controller = meta->controller_user_id;

		//JAMNET_LOG_DEBUG("netId= {}, spawnReqId= {}, owner= {}, controller= {}", netId, spawnReqId, owner, controller);

		if (spawnReqId != 0)
		{
			if (e = netWorld->TryConfirmPendingSpawn(spawnReqId, netId); e != entt::null)
				return e;
		}

		return netWorld->EnsureReplicatedActor(netId, prefabKey, owner, controller);
	}



	void ClientReplicationSystem::ApplyRigidFullSnapshot(uint64 serverTick, uint32 netId, const fb::fbTransformFull* tf, uint32 baselineRev)
	{
		px::RigidState unpacked{};
		if (!UnpackRigidFull192(tf->data0(), tf->data1(), tf->data2(), unpacked))
			return;

		Replica& replica = GetOrCreateReplica(netId);
		replica.lastSeenTick	= serverTick;
		replica.baselineRev		= baselineRev;
		replica.baselinePos		= unpacked.pose.p;
		replica.baselineRot		= unpacked.pose.q;
		replica.hasBaseline		= true;

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyFullSnapshot] : Invalid entity")
			return;
		}

		auto& rs = m_world.get_or_emplace<px::RigidState>(replica.e);
		rs = unpacked;
	}

	void ClientReplicationSystem::ApplyRigidDeltaSnapshot(uint64 serverTick, uint32 netId, const fb::fbTransformDelta* tf, uint32 baselineRev)
	{
		Replica& replica = GetOrCreateReplica(netId);
		replica.lastSeenTick = serverTick;

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyDeltaSnapshot] : Invalid entity")
				return;
		}

		if (!replica.hasBaseline) return;

		if (baselineRev != replica.baselineRev)
		{
			replica.hasBaseline = false;
			return;
		}

		px::RigidState unpacked{};
		if (!UnpackRigidDelta128(replica.baselinePos, replica.baselineRot, tf->data0(), tf->data1(), unpacked))
			return;

		auto& rs = m_world.get_or_emplace<px::RigidState>(replica.e);
		rs = unpacked;

		replica.baselinePos		= unpacked.pose.p;
		replica.baselineRot		= unpacked.pose.q;
		replica.baselineRev		= baselineRev + 1;
	}

	void ClientReplicationSystem::ApplyCharacterFullSnapshot(uint64 serverTick, uint32 netId, const fb::fbCharacterFull160* ch, uint32 baselineRev, bool isLocal)
	{
		px::CharacterState unpacked{};
		if (!UnpackCharacterFull160(ch->data0(), ch->data1(), ch->data2(), unpacked))
			return;

		//JAMNET_LOG_TRACE("Character	pos({}, {}, {})", unpacked.pos.x, unpacked.pos.y, unpacked.pos.z);

		Replica& replica = GetOrCreateReplica(netId);
		replica.lastSeenTick	= serverTick;
		replica.baselineRev		= baselineRev;
		replica.baselinePos		= unpacked.pos;
		replica.baselineYaw		= unpacked.facingYaw;
		replica.baselinePitch	= unpacked.facingPitch;
		replica.hasBaseline		= true;

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyCharacterFullSnapshot] : Invalid entity")
			return;
		}

		if (isLocal)
		{
			ServerState state{};
			state.serverTick = serverTick;
			state.inputAck	 = m_lastInputAck;
			state.state		 = unpacked;

			auto& queue = m_world.ctx().get<PendingServerStateQueue>();
			queue.states.push_back(state);

			if (!m_world.all_of<px::CharacterState>(replica.e))
				m_world.emplace<px::CharacterState>(replica.e, unpacked);

			return;
		}

		auto& cs = m_world.get_or_emplace<px::CharacterState>(replica.e);
		cs = unpacked;
	}

	void ClientReplicationSystem::ApplyCharacterDeltaSnapshot(uint64 serverTick, uint32 netId, const fb::fbCharacterDelta128* ch, uint32 baselineRev, bool isLocal)
	{
		Replica& replica = GetOrCreateReplica(netId);
		replica.lastSeenTick = serverTick;

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyCharacterDeltaSnapshot] : Invalid entity")
				return;
		}

		if (!replica.hasBaseline)
			return;

		if (baselineRev != replica.baselineRev)
		{
			replica.hasBaseline = false;
			return;
		}

		px::CharacterState unpacked{};
		if (!UnpackCharacterDelta128(replica.baselinePos, replica.baselineYaw, replica.baselinePitch, ch->data0(), ch->data1(), unpacked))
			return;

		//JAMNET_LOG_TRACE("Character	pos({}, {}, {})", unpacked.pos.x, unpacked.pos.y, unpacked.pos.z);

		replica.baselinePos		= unpacked.pos;
		replica.baselineYaw		= unpacked.facingYaw;
		replica.baselinePitch	= unpacked.facingPitch;
		replica.baselineRev		= baselineRev + 1;

		if (isLocal)
		{
			ServerState state{};
			state.serverTick = serverTick;
			state.inputAck	 = m_lastInputAck;
			state.state		 = unpacked;

			auto& queue = m_world.ctx().get<PendingServerStateQueue>();
			queue.states.push_back(state);

			if (!m_world.all_of<px::CharacterState>(replica.e))
				m_world.emplace<px::CharacterState>(replica.e, unpacked);

			return;
		}

		auto& cs = m_world.get_or_emplace<px::CharacterState>(replica.e);
		cs = unpacked;
	}

	Replica& ClientReplicationSystem::GetOrCreateReplica(uint32 netId, bool* created)
	{
		auto it = m_replicas.find(netId);
		if (it != m_replicas.end())
		{
			if (created) *created = false;
			return it->second;
		}

		Replica rep{};
		rep.netId = netId;
		auto [iter, _] = m_replicas.emplace(netId, rep);
		if (created) *created = true;
		return iter->second;
	}

	void ClientReplicationSystem::PruneOldReplicas(uint64 serverTick, uint64 forgetAfterTicks)
	{
		vector<uint64> toErase;
		for (const auto& [id, replica] : m_replicas)
		{
			if (serverTick > replica.lastSeenTick && (serverTick - replica.lastSeenTick) > forgetAfterTicks)
				toErase.push_back(id);
		}

		for (uint64 id : toErase)
		{
			auto it = m_replicas.find(id);
			if (it == m_replicas.end())
				continue;

			m_replicas.erase(it);
		}
	}

	void ClientReplicationSystem::UpdateUniqueLocalFromMeta(uint32 netId, const fb::fbActorMetaT& meta, Replica& replica)
	{
		const uint64 owner		= meta.owner_user_id;
		const uint64 controller = meta.controller_user_id;

		const bool isLocalCandidate = (owner != 0 && owner == m_userId && controller == m_userId);
		if (!isLocalCandidate)
			return;

		if (m_localNetId == netId && m_localEntity == replica.e)
		{
			replica.isLocal = true;
			return;
		}

		if (m_localNetId != 0)
		{
			if (auto it = m_replicas.find(m_localNetId); it != m_replicas.end())
				it->second.isLocal = false;
		}

		m_localNetId	= netId;
		m_localEntity	= replica.e;

		replica.isLocal = true;
	}
}
