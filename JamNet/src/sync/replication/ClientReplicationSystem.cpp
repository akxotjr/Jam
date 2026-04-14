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
		m_pendingLifecycle.clear();
		m_pendingSnapshots.clear();
		m_localNetId = NetId::Invalid();
		m_localEntity = entt::null;
		ClearLocalActorRef();
		m_lastServerTick = 0;
		m_lastInputAck = 0;

		if (auto* est = m_world.ctx().find<EstimatedServerTick>())
			est->Reset();
	}

	void ClientReplicationSystem::Tick()
	{
		while (!m_pendingLifecycle.empty())
		{
			PendingLifecycleBatch pending = std::move(m_pendingLifecycle.front());
			m_pendingLifecycle.pop_front();

			m_lastServerTick = std::max<uint64>(pending.batch.server_tick, m_lastServerTick);

			for (const auto& actorPtr : pending.batch.actors)
			{
				if (!actorPtr) continue;
				ProcessLifecycleActor(*actorPtr);
			}
		}

		while (!m_pendingSnapshots.empty())
		{
			PendingSnapshot pending = std::move(m_pendingSnapshots.front());
			m_pendingSnapshots.pop_front();

			fb::fbSnapshotT& snapshot = pending.snapshot;

			const auto* hdr = snapshot.header.get();
			if (!hdr) continue;

			const uint64 serverTick = hdr->server_tick;
			const uint32 inputAck   = hdr->input_ack;
			const uint32 inputEpoch = hdr->input_epoch;

			if (serverTick < m_lastServerTick)
				continue;

			m_lastServerTick = serverTick;
			m_lastInputAck   = std::max(m_lastInputAck, inputAck);

			if (auto* est = m_world.ctx().find<EstimatedServerTick>())
				est->Update(serverTick, pending.recvNs, NOW_NS());

			for (const auto& entPtr : snapshot.entities)
			{
				if (!entPtr) continue;
				ProcessEntity(*entPtr, serverTick, inputEpoch);
			}

			ResolveDeferredTargetBindingsAndSpawn();
			PruneOldReplicas(serverTick);
		}
	}

	void ClientReplicationSystem::EnqueueLifecycle(fb::fbLifecycleBatchT batch)
	{
		m_pendingLifecycle.emplace_back(PendingLifecycleBatch{
			.batch = std::move(batch)
		});
	}

	void ClientReplicationSystem::EnqueueSnapshot(fb::fbSnapshotT snapshot, uint64 recvNs)
	{
		m_pendingSnapshots.emplace_back(PendingSnapshot{
			.snapshot = std::move(snapshot),
			.recvNs = recvNs
		});
	}

	void ClientReplicationSystem::ProcessLifecycleActor(const fb::fbLifecycleActorT& actor)
	{
		const NetId netId = NetId::MakeRaw(actor.net_id);
		if (!netId.IsValid())
			return;

		auto* ctx = m_world.ctx().find<ClientNetWorld*>();
		if (!ctx || !*ctx)
			return;

		ClientNetWorld* netWorld = *ctx;

		if (actor.op == fb::fbLifecycleOp_Remove)
		{
			if (actor.remove_reason == fb::fbRemovalReason_Destroyed)
			{
				netWorld->DestroyReplicatedActor(netId);
				m_replicas.erase(netId);

				if (m_localNetId == netId)
				{
					m_localNetId = NetId::Invalid();
					m_localEntity = entt::null;
					ClearLocalActorRef();
				}
				return;
			}

			netWorld->SetReplicatedActorDormant(netId);
			return;
		}

		const auto* meta = actor.meta.get();
		if (!meta) return;

		entt::entity entity = entt::null;
		if (meta->spawn_req_id != 0)
		{
			if (meta->owner_user_id == m_userId)
			{
				entity = netWorld->TryConfirmPendingSpawn(netId, meta->spawn_req_id);
			}
		}

		if (entity == entt::null || !m_world.valid(entity))
			entity = netWorld->EnsureReplicatedActor(netId, px::PrefabKey{ meta->prefab_key }, meta->owner_user_id, meta->controller_user_id);

		if (entity == entt::null || !m_world.valid(entity))
			return;

		Replica& replica = GetOrCreateReplica(netId);
		replica.e = entity;
		ApplyActorMeta(netId, entity, *meta, replica);

		const bool wasHidden = m_world.all_of<OutOfAoiTag>(entity) || m_world.all_of<PredictedDespawnTag>(entity);
		if (wasHidden)
			netWorld->ReactivateReplicatedActor(netId, replica.isLocal);
	}

	void ClientReplicationSystem::ApplyActorMeta(NetId netId, entt::entity entity, const fb::fbActorMetaT& meta, Replica& replica)
	{
		if (entity == entt::null || !m_world.valid(entity))
			return;

		m_world.emplace_or_replace<NetPrefabKey>(entity, NetPrefabKey{ px::PrefabKey{ meta.prefab_key } });
		m_world.emplace_or_replace<OwnershipTag>(entity, OwnershipTag{ meta.owner_user_id });
		m_world.emplace_or_replace<ControlTag>(entity, ControlTag{ meta.controller_user_id });
		m_world.emplace_or_replace<NetTeamPartRole>(entity, NetTeamPartRole::FromPacked(meta.packed_id));

		replica.e = entity;
		UpdateUniqueLocalFromMeta(netId, meta, replica);
	}

	void ClientReplicationSystem::ProcessEntity(const fb::fbActorEntityT& ent, uint64 serverTick, uint32 inputEpoch)
	{
		const NetId nid = NetId::MakeRaw(ent.net_id);
		const uint32 baselineRev = ent.baseline_rev;

		const bool hasFull		= (ent.transform_full != nullptr);
		const bool hasDelta		= (ent.transform_delta != nullptr);
		const bool hasKine		= (ent.kinematic_state != nullptr);
		const bool hasCharFull  = (ent.character_full != nullptr);
		const bool hasCharDelta = (ent.character_delta != nullptr);

		const entt::entity resolved = ResolveEntityForSnapshot(nid);
		if (resolved == entt::null || !m_world.valid(resolved))
			return;

		Replica& replica = GetOrCreateReplica(nid);
		replica.e			 = resolved;
		replica.lastSeenTick = serverTick;

		const bool wasHidden = m_world.all_of<OutOfAoiTag>(resolved) || m_world.all_of<PredictedDespawnTag>(resolved);

		if (hasCharFull)
		{
			ApplyCharacterFullSnapshot(serverTick, nid, ent.character_full.get(), baselineRev, replica.isLocal, inputEpoch);
		}
		else if (hasCharDelta)
		{
			ApplyCharacterDeltaSnapshot(serverTick, nid, ent.character_delta.get(), baselineRev, replica.isLocal, inputEpoch);
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

		const bool hasResolvedSpawnState =
			hasCharFull
			|| hasFull
			|| hasKine
			|| ((hasCharDelta || hasDelta) && replica.hasBaseline);

		if (nid.IsLevel())
			return;

		if (!hasResolvedSpawnState)
			return;

		if (m_world.all_of<PhysicsSpawnedTag>(resolved))
			return;

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
						return;
				}
			}
		}

		if (wasHidden)
		{
			if (auto* worldPtr = m_world.ctx().find<ClientNetWorld*>(); worldPtr && *worldPtr)
				(*worldPtr)->ReactivateReplicatedActor(nid, replica.isLocal);
		}

		if (auto* phys = m_world.ctx().find<ClientPhysicsSystem>())
			phys->SpawnActor(resolved, replica.isLocal);
	}

	entt::entity ClientReplicationSystem::ResolveEntityForSnapshot(NetId netId)
	{
		auto* ctx = m_world.ctx().find<ClientNetWorld*>();
		if (!ctx || !*ctx)
			return entt::null;

		return (*ctx)->GetEntity(netId);
	}

	void ClientReplicationSystem::ApplyRigidFullSnapshot(uint64 serverTick, NetId netId, const fb::fbTransformFull* tf, uint32 baselineRev)
	{
		px::RigidState unpacked{};
		if (!UnpackRigidFull192(tf->data0(), tf->data1(), tf->data2(), unpacked))
			return;

		Replica& replica = GetOrCreateReplica(netId);
		replica.lastSeenTick = serverTick;
		replica.baselineRev = baselineRev;
		replica.baselinePos = unpacked.pose.p;
		replica.baselineRot = unpacked.pose.q;
		replica.hasBaseline = true;

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyFullSnapshot] : Invalid entity");
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
			JAM_CRASH("[ApplyDeltaSnapshot] : Invalid entity");
			return;
		}

		if (!replica.hasBaseline)
			return;

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

		replica.baselinePos = unpacked.pose.p;
		replica.baselineRot = unpacked.pose.q;
		replica.baselineRev = baselineRev + 1;
	}

	void ClientReplicationSystem::ApplyKinematicStateSnapshot(uint64 serverTick, NetId netId, const fb::fbKinematicState* ks, uint32 baselineRev)
	{
		if (!ks) return;

		Replica& replica = GetOrCreateReplica(netId);
		replica.lastSeenTick = serverTick;
		replica.baselineRev = baselineRev;
		replica.hasBaseline = false;

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyKinematicStateSnapshot] : Invalid entity");
			return;
		}

		const px::eKineDrivenType kineType = static_cast<px::eKineDrivenType>(ks->kine_type());

		px::KinematicState kine{};
		kine.startEpoch = ks->start_epoch();
		kine.phase = ks->phase();
		kine.t = ks->t();
		kine.eventMask = ks->event_mask();

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
		rs.kineType = kineType;
		rs.kineState = kine;
	}

	void ClientReplicationSystem::ApplyCharacterFullSnapshot(uint64 serverTick, NetId netId, const fb::fbCharacterFull160* ch, uint32 baselineRev, bool isLocal, uint32 inputEpoch)
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
			JAM_CRASH("[ApplyCharacterFullSnapshot] : Invalid entity");
			return;
		}

		if (isLocal)
		{
			auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
			cs = unpacked;

			auto& signal = m_world.ctx().get<ReconcileSignal>();
			if (m_lastInputAck > signal.inputAck && inputEpoch >= GetCurrentLocalCommandEpoch())
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

	void ClientReplicationSystem::ApplyCharacterDeltaSnapshot(uint64 serverTick, NetId netId, const fb::fbCharacterDelta128* ch, uint32 baselineRev, bool isLocal, uint32 inputEpoch)
	{
		Replica& replica = GetOrCreateReplica(netId);
		replica.lastSeenTick = serverTick;

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyCharacterDeltaSnapshot] : Invalid entity");
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

		replica.baselinePos   = unpacked.pos;
		replica.baselineYaw   = unpacked.facingYaw;
		replica.baselinePitch = unpacked.facingPitch;
		replica.baselineRev	  = baselineRev + 1;

		if (isLocal)
		{
			auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
			cs = unpacked;

			auto& signal = m_world.ctx().get<ReconcileSignal>();
			if (m_lastInputAck > signal.inputAck && inputEpoch >= GetCurrentLocalCommandEpoch())
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
			m_replicas.erase(id);
	}

	void ClientReplicationSystem::UpdateUniqueLocalFromMeta(NetId netId, const fb::fbActorMetaT& meta, Replica& replica)
	{
		const uint64 owner = meta.owner_user_id;
		const uint64 controller = meta.controller_user_id;

		const bool isLocalCandidate = (owner != 0 && owner == m_userId && controller == m_userId);
		const bool wasLocal = (m_localNetId == netId && m_localEntity == replica.e);

		if (!isLocalCandidate)
		{
			replica.isLocal = false;

			if (wasLocal)
			{
				m_localNetId  = NetId::Invalid();
				m_localEntity = entt::null;
				ClearLocalActorRef();

				if (replica.e != entt::null && m_world.valid(replica.e))
				{
					m_world.remove<LocalActorTag>(replica.e);
					m_world.emplace_or_replace<RemoteActorTag>(replica.e);
				}
			}

			return;
		}

		if (m_localNetId == netId && m_localEntity == replica.e)
		{
			replica.isLocal = true;
			SetLocalActorRef(netId, replica.e);
			if (replica.e != entt::null && m_world.valid(replica.e))
			{
				m_world.remove<RemoteActorTag>(replica.e);
				m_world.emplace_or_replace<LocalActorTag>(replica.e);
			}
			return;
		}

		if (m_localNetId.IsValid())
		{
			if (auto it = m_replicas.find(m_localNetId); it != m_replicas.end())
			{
				it->second.isLocal = false;
				if (it->second.e != entt::null && m_world.valid(it->second.e))
				{
					m_world.remove<LocalActorTag>(it->second.e);
					m_world.emplace_or_replace<RemoteActorTag>(it->second.e);
				}
			}
		}

		m_localNetId  = netId;
		m_localEntity = replica.e;
		SetLocalActorRef(netId, replica.e);

		replica.isLocal = true;
		if (replica.e != entt::null && m_world.valid(replica.e))
		{
			m_world.remove<RemoteActorTag>(replica.e);
			m_world.emplace_or_replace<LocalActorTag>(replica.e);
		}
	}

	void ClientReplicationSystem::ResolveDeferredTargetBindingsAndSpawn()
	{
		auto* phys = m_world.ctx().find<ClientPhysicsSystem>();
		if (!phys) return;

		auto view = m_world.view<NetId, NetActorBodyType, RigidAuthorityState, TargetInfo>(entt::exclude<PhysicsSpawnedTag, PredictedDespawnTag>);

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

	uint32 ClientReplicationSystem::GetCurrentLocalCommandEpoch() const
	{
		if (auto* nwPtr = m_world.ctx().find<ClientNetWorld*>(); nwPtr && *nwPtr)
			return (*nwPtr)->GetLatestLocalCommandEpoch();

		return 0;
	}

	void ClientReplicationSystem::SetLocalActorRef(NetId netId, entt::entity entity)
	{
		if (auto* ref = m_world.ctx().find<LocalActorRef>())
		{
			ref->netId = netId;
			ref->entity = entity;
		}
	}

	void ClientReplicationSystem::ClearLocalActorRef()
	{
		if (auto* ref = m_world.ctx().find<LocalActorRef>())
			ref->Clear();
	}
}
