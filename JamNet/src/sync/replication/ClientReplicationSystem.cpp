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
		m_localNetId		= NetId::Invalid();
		m_localEntity		= entt::null;
		m_lastServerTick	= 0;
		m_lastInputAck		= 0;

		if (auto* est = m_world.ctx().find<EstimatedServerTick>())
			est->Reset();
	}

	void ClientReplicationSystem::Tick()
	{
		while (!m_pendingSnapshots.empty())
		{
			PendingSnapshot pending = std::move(m_pendingSnapshots.front());
			m_pendingSnapshots.pop_front();

			fb::fbSnapshotT& snapshot = pending.snapshot;

			const auto* hdr = snapshot.header.get();
			if (!hdr) continue;

			const uint64 serverTick = hdr->server_tick;
			const uint32 inputAck   = hdr->input_ack;
			
			if (serverTick < m_lastServerTick)
				continue;

			m_lastServerTick = serverTick;
			m_lastInputAck	 = std::max(m_lastInputAck, inputAck);

			if (auto* est = m_world.ctx().find<EstimatedServerTick>())
				est->Update(serverTick, pending.recvNs, NOW_NS());

			for (const auto& entPtr : snapshot.entities)
			{
				if (!entPtr) continue;
				ProcessEntity(*entPtr, serverTick);
			}

			ResolveDeferredTargetBindingsAndSpawn();

			PruneOldReplicas(serverTick);
		}
	}


	void ClientReplicationSystem::EnqueueSnapshot(fb::fbSnapshotT snapshot, uint64 recvNs)
	{
		m_pendingSnapshots.emplace_back(PendingSnapshot{
			.snapshot = std::move(snapshot),
			.recvNs = recvNs
		});
	}

	void ClientReplicationSystem::ProcessEntity(const fb::fbActorEntityT& ent, uint64 serverTick)
	{
		const NetId  nid		 = NetId::MakeRaw(ent.net_id);
		const uint32 baselineRev = ent.baseline_rev;

		const bool hasFull		= (ent.transform_full  != nullptr);
		const bool hasDelta		= (ent.transform_delta != nullptr);
		const bool hasKine		= (ent.kinematic_state != nullptr);
		const bool hasCharFull	= (ent.character_full  != nullptr);
		const bool hasCharDelta = (ent.character_delta != nullptr);

		const entt::entity resolved = ResolveEntityForSnapshot(nid, ent.meta.get());
		if (resolved == entt::null || !m_world.valid(resolved))
			return;

		Replica& replica = GetOrCreateReplica(nid);
		replica.e			 = resolved;
		replica.lastSeenTick = serverTick;

		if (const auto* meta = ent.meta.get())
		{
			UpdateUniqueLocalFromMeta(nid, *meta, replica);
			m_world.emplace_or_replace<NetTeamPartRole>(resolved, NetTeamPartRole::FromPacked(meta->packed_id));
		}

		if (hasCharFull)
		{
			ApplyCharacterFullSnapshot(serverTick, nid, ent.character_full.get(), baselineRev, replica.isLocal);
		}
		else if (hasCharDelta)
		{
			ApplyCharacterDeltaSnapshot(serverTick, nid, ent.character_delta.get(), baselineRev, replica.isLocal);
		}
		else if (hasKine)
		{
			ApplyKinematicStateSnapshot(serverTick, nid, ent.kinematic_state.get(), baselineRev);
		}
		else if (hasFull)
		{
			ApplyRigidFullSnapshot(serverTick, nid, ent.transform_full.get(), baselineRev);
		}
		else if (hasDelta)
		{
			ApplyRigidDeltaSnapshot(serverTick, nid, ent.transform_delta.get(), baselineRev);
		}

		if (nid.IsLevel())
			return;

		if (m_world.all_of<PhysicsSpawnedTag>(resolved))
			return;

		// TargetDerived는 target resolve 전에는 스폰 지연
		if (m_world.all_of<NetActorBodyType, RigidAuthorityState>(resolved))
		{
			const auto bodyType = m_world.get<NetActorBodyType>(resolved).body;
			if (bodyType == px::eBodyType::Rigid)
			{
				const auto& auth = m_world.get<RigidAuthorityState>(resolved).state;
				if (auth.kineType == px::eKineDrivenType::TargetDerived)
				{
					const auto* ti = m_world.try_get<TargetInfo>(resolved);
					if (!ti || ti->targetObjId == px::INVALID_OBJ_ID)
						return; // defer spawn
				}
			}
		}

		if (auto* phys = m_world.ctx().find<ClientPhysicsSystem>())
			phys->SpawnActor(resolved, replica.isLocal);
	}

	entt::entity ClientReplicationSystem::ResolveEntityForSnapshot(NetId netId, const fb::fbActorMetaT* meta)
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

	
		if (spawnReqId != 0)
		{
			if (e = netWorld->TryConfirmPendingSpawn(netId, spawnReqId); e != entt::null)
				return e;
		}

		return netWorld->EnsureReplicatedActor(netId, prefabKey, owner, controller);
	}



	void ClientReplicationSystem::ApplyRigidFullSnapshot(uint64 serverTick, NetId netId, const fb::fbTransformFull* tf, uint32 baselineRev)
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

		auto& [rs] = m_world.get<RigidAuthorityState>(replica.e);
		rs = unpacked;
	}

	void ClientReplicationSystem::ApplyRigidDeltaSnapshot(uint64 serverTick, NetId netId, const fb::fbTransformDelta* tf, uint32 baselineRev)
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

		auto& [rs] = m_world.get<RigidAuthorityState>(replica.e);
		rs = unpacked;

		replica.baselinePos	= unpacked.pose.p;
		replica.baselineRot	= unpacked.pose.q;
		replica.baselineRev	= baselineRev + 1;
	}

	void ClientReplicationSystem::ApplyKinematicStateSnapshot(uint64 serverTick, NetId netId, const fb::fbKinematicState* ks, uint32 baselineRev)
	{
		if (!ks) return;

		Replica& replica = GetOrCreateReplica(netId);
		replica.lastSeenTick = serverTick;
		replica.baselineRev  = baselineRev;
		replica.hasBaseline  = false; // kinematic_state 경로는 rigid delta baseline 미사용

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyKinematicStateSnapshot] : Invalid entity")
				return;
		}

		const px::eKineDrivenType kineType = static_cast<px::eKineDrivenType>(ks->kine_type());

		px::KinematicState kine{};
		kine.startEpoch = ks->start_epoch();
		kine.phase		= ks->phase();
		kine.t			= ks->t();
		kine.eventMask	= ks->event_mask();

		const NetId targetNetId = NetId::MakeRaw(ks->target_id());
		kine.targetId = px::INVALID_OBJ_ID;

		TargetInfo targetInfo{};
		targetInfo.targetNetId = targetNetId;
		targetInfo.targetObjId = px::INVALID_OBJ_ID;

		if (targetNetId.IsValid())
		{
			px::ObjectId resolved = px::INVALID_OBJ_ID;
			if (TryResolveTargetObjId(targetNetId, resolved))
			{
				kine.targetId = resolved;
				targetInfo.targetObjId = resolved;
			}
		}
		m_world.emplace_or_replace<TargetInfo>(replica.e, targetInfo);

		if (px::IsLocalDrivenKine(kineType))
		{
			if (const auto* est = m_world.ctx().find<EstimatedServerTick>(); est && est->valid)
			{
				const double dtTick = est->estimatedNowTick - static_cast<double>(serverTick);
				if (dtTick > 0.0)
					kine.t += static_cast<float>(dtTick * static_cast<double>(SIMULATION_TICK_SEC));
			}
		}

		auto& [rs] = m_world.get<RigidAuthorityState>(replica.e);
		rs.kineType  = kineType;
		rs.kineState = kine;
	}

	void ClientReplicationSystem::ApplyCharacterFullSnapshot(uint64 serverTick, NetId netId, const fb::fbCharacterFull160* ch, uint32 baselineRev, bool isLocal)
	{
		px::CharacterState unpacked{};
		if (!UnpackCharacterFull160(ch->data0(), ch->data1(), ch->data2(), unpacked))
			return;

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
			auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
			cs = unpacked;

			auto& signal = m_world.ctx().get<ReconcileSignal>();
			if (serverTick > signal.serverTick || m_lastInputAck >= signal.inputAck)
			{
				signal.serverTick = serverTick;
				signal.inputAck   = m_lastInputAck;
				signal.dirty	  = true;
			}

			auto& history = m_world.get<CharReplayHistory>(replica.e);
			history.Push(serverTick, unpacked);

			return;
		}

		auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
		cs = unpacked;
	}

	void ClientReplicationSystem::ApplyCharacterDeltaSnapshot(uint64 serverTick, NetId netId, const fb::fbCharacterDelta128* ch, uint32 baselineRev, bool isLocal)
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

		replica.baselinePos		= unpacked.pos;
		replica.baselineYaw		= unpacked.facingYaw;
		replica.baselinePitch	= unpacked.facingPitch;
		replica.baselineRev		= baselineRev + 1;

		if (isLocal)
		{
			auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
			cs = unpacked;

			auto& signal = m_world.ctx().get<ReconcileSignal>();
			if (serverTick > signal.serverTick || m_lastInputAck >= signal.inputAck)
			{
				signal.serverTick = serverTick;
				signal.inputAck   = m_lastInputAck;
				signal.dirty      = true;
			}

			auto& history = m_world.get<CharReplayHistory>(replica.e);
			history.Push(serverTick, unpacked);

			return;
		}

		auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
		cs = unpacked;
	}

	Replica& ClientReplicationSystem::GetOrCreateReplica(NetId netId, bool* created)
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
		std::vector<NetId> toErase;
		for (const auto& [id, replica] : m_replicas)
		{
			if (serverTick > replica.lastSeenTick && (serverTick - replica.lastSeenTick) > forgetAfterTicks)
				toErase.push_back(id);
		}

		for (NetId id : toErase)
		{
			auto it = m_replicas.find(id);
			if (it == m_replicas.end())
				continue;

			m_replicas.erase(it);
		}
	}

	void ClientReplicationSystem::UpdateUniqueLocalFromMeta(NetId netId, const fb::fbActorMetaT& meta, Replica& replica)
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

		if (m_localNetId.IsValid())
		{
			if (auto it = m_replicas.find(m_localNetId); it != m_replicas.end())
				it->second.isLocal = false;
		}

		m_localNetId  = netId;
		m_localEntity = replica.e;

		replica.isLocal = true;
	}

	void ClientReplicationSystem::ResolveDeferredTargetBindingsAndSpawn()
	{
		auto* phys = m_world.ctx().find<ClientPhysicsSystem>();
		if (!phys) return;

		auto view = m_world.view<NetId, NetActorBodyType, RigidAuthorityState, TargetInfo>(entt::exclude<PhysicsSpawnedTag>);

		for (auto e : view)
		{
			const auto bodyType = view.get<NetActorBodyType>(e).body;
			if (bodyType != px::eBodyType::Rigid)
				continue;

			const auto& auth = view.get<RigidAuthorityState>(e).state;
			if (auth.kineType != px::eKineDrivenType::TargetDerived)
				continue;

			auto& ti = view.get<TargetInfo>(e);
			if (!ti.targetNetId.IsValid())
				continue;

			if (ti.targetObjId == px::INVALID_OBJ_ID)
			{
				px::ObjectId resolved = px::INVALID_OBJ_ID;
				if (!TryResolveTargetObjId(ti.targetNetId, resolved))
					continue;

				ti.targetObjId = resolved;
			}

			const NetId nid = view.get<NetId>(e);
			Replica& replica = GetOrCreateReplica(nid);
			phys->SpawnActor(e, replica.isLocal);
		}
	}

	bool ClientReplicationSystem::TryResolveTargetObjId(NetId targetNetId, px::ObjectId& outObjId)
	{
		outObjId = px::INVALID_OBJ_ID;

		if (!targetNetId.IsValid())
			return false;

		auto* nwPtr = m_world.ctx().find<ClientNetWorld*>();
		if (!nwPtr || !*nwPtr)
			return false;

		const entt::entity targetEntity = (*nwPtr)->GetEntity(targetNetId);
		if (targetEntity == entt::null || !m_world.valid(targetEntity))
			return false;

		outObjId = MakeObjectId(targetEntity);
		return true;
	}
}
