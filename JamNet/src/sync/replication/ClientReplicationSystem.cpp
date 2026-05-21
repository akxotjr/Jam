#include "pch.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationCodec.h"
#include "jamnet/sync/replication/ClientReplicationSystem.h"

#include "jamnet/sync/networld/ClientPhysicalWorld.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/WorldContext.h"

namespace jam::net
{
	ClientReplicationSystem::ClientReplicationSystem(entt::registry& world)
		: m_world(world)
	{
	}

	void ClientReplicationSystem::Init()
	{
		Clear();

		if (auto* nw = m_world.ctx().find<ClientPhysicalWorld*>())
			m_netWorld = (nw && *nw) ? *nw : nullptr;
		m_clientPhysics			= m_world.ctx().find<ClientPhysicsSystem>();
		m_estimatedServerTick	= m_world.ctx().find<EstimatedServerTick>();
		m_localActorRef			= m_world.ctx().find<LocalActorRef>();
		m_reconcileSignal		= m_world.ctx().find<ReconcileSignal>();

		m_userId = m_netWorld ? m_netWorld->GetUserId() : 0;
	}

	void ClientReplicationSystem::Clear()
	{
		m_replicas.clear();
		m_pendingLifecycle.clear();
		m_pendingSnapshotBatches.clear();
		m_localNetId = NetId::Invalid();
		m_localEntity = entt::null;
		ClearLocalActorRef();
		m_lastServerTick = 0;
		m_lastLifecycleTick = 0;
		m_latestQueuedSnapshotTick = 0;
		m_lastAppliedSnapshotTick = 0;
		m_lastInputAck = 0;

		if (m_estimatedServerTick)
			m_estimatedServerTick->Reset();

		m_netWorld = nullptr;
		m_clientPhysics = nullptr;
		m_estimatedServerTick = nullptr;
		m_localActorRef = nullptr;
		m_reconcileSignal = nullptr;
	}

	void ClientReplicationSystem::Tick()
	{
		if (m_clientPhysics == nullptr)
			m_clientPhysics = m_world.ctx().find<ClientPhysicsSystem>();

		while (!m_pendingLifecycle.empty())
		{
			PendingLifecycleBatch pending = std::move(m_pendingLifecycle.front());
			m_pendingLifecycle.pop_front();

			m_lastLifecycleTick = std::max<uint64>(pending.batch.server_tick, m_lastLifecycleTick);
			m_lastServerTick = std::max(m_lastLifecycleTick, m_lastAppliedSnapshotTick);

			for (const auto& actorPtr : pending.batch.actors)
			{
				if (!actorPtr) continue;
				ProcessLifecycleActor(*actorPtr);
			}
		}

		if (m_pendingSnapshotBatches.empty())
			return;

		// Snapshot delivery on UNRELIABLE_SEQUENCED is freshness-biased and drop-tolerant.
		// Older incomplete ticks must never block the newest available tick.
		const uint64 latestQueuedTick = std::max(m_latestQueuedSnapshotTick, m_pendingSnapshotBatches.rbegin()->first);
		if (latestQueuedTick <= m_lastAppliedSnapshotTick)
		{
			m_pendingSnapshotBatches.clear();
			return;
		}

		for (auto it = m_pendingSnapshotBatches.begin(); it != m_pendingSnapshotBatches.end();)
		{
			if (it->first >= latestQueuedTick)
				break;
			it = m_pendingSnapshotBatches.erase(it);
		}

		auto batchIt = m_pendingSnapshotBatches.find(latestQueuedTick);
		if (batchIt == m_pendingSnapshotBatches.end())
			return;

		PendingSnapshotBatch& batch = batchIt->second;

		for (uint16 chunkIndex = 0; chunkIndex < batch.expectedChunkCount; ++chunkIndex)
		{
			if (chunkIndex >= batch.chunks.size() || !batch.chunks[chunkIndex].has_value())
				continue;

			PendingSnapshot pending = std::move(*batch.chunks[chunkIndex]);
			batch.chunks[chunkIndex].reset();

			fb::fbSnapshotT& snapshot = pending.snapshot;
			const auto* hdr = snapshot.header.get();
			if (!hdr)
				continue;

			for (const auto& entPtr : snapshot.entities)
			{
				if (!entPtr) continue;
				ProcessEntity(*entPtr, batch.serverTick, batch.inputEpoch);
			}
		}

		if (m_estimatedServerTick)
			m_estimatedServerTick->Update(batch.serverTick, batch.firstRecvNs, NOW_NS());

		m_lastAppliedSnapshotTick = batch.serverTick;
		m_lastServerTick = std::max(m_lastLifecycleTick, m_lastAppliedSnapshotTick);
		m_lastInputAck   = std::max(m_lastInputAck, batch.inputAck);
		ResolveDeferredTargetBindingsAndSpawn();
		PruneOldReplicas(batch.serverTick);

		m_pendingSnapshotBatches.erase(batchIt);
		m_latestQueuedSnapshotTick = m_pendingSnapshotBatches.empty() ? 0 : m_pendingSnapshotBatches.rbegin()->first;
	}

	void ClientReplicationSystem::EnqueueLifecycle(fb::fbLifecycleBatchT batch)
	{
		m_pendingLifecycle.emplace_back(PendingLifecycleBatch{
			.batch = std::move(batch)
		});
	}

	void ClientReplicationSystem::EnqueueSnapshot(fb::fbSnapshotT snapshot, uint64 recvNs)
	{
		const auto* hdr = snapshot.header.get();
		if (!hdr)
			return;

		const uint64 serverTick = hdr->server_tick;
		if (serverTick <= m_lastAppliedSnapshotTick)
			return;

		m_latestQueuedSnapshotTick = std::max(m_latestQueuedSnapshotTick, serverTick);

		const uint16 chunkIndex = hdr->chunk_index;
		const uint16 chunkCount = std::max<uint16>(1, hdr->chunk_count);
		if (chunkIndex >= chunkCount)
			return;

		PendingSnapshotBatch& batch = m_pendingSnapshotBatches[serverTick];
		if (batch.serverTick == 0)
		{
			batch.serverTick		 = serverTick;
			batch.inputAck			 = hdr->input_ack;
			batch.inputEpoch		 = hdr->input_epoch;
			batch.expectedChunkCount = chunkCount;
			batch.firstRecvNs		 = recvNs;
		}

		batch.inputAck			 = std::max(batch.inputAck, hdr->input_ack);
		batch.inputEpoch		 = hdr->input_epoch;
		batch.expectedChunkCount = std::max(batch.expectedChunkCount, chunkCount);
		batch.lastRecvNs		 = recvNs;

		if (batch.chunks.size() < batch.expectedChunkCount)
			batch.chunks.resize(batch.expectedChunkCount);

		batch.chunks[chunkIndex] = PendingSnapshot{
			.snapshot = std::move(snapshot),
			.recvNs   = recvNs
		};

	}

	void ClientReplicationSystem::ProcessLifecycleActor(const fb::fbLifecycleActorT& actor)
	{
		const NetId netId = NetId::MakeRaw(actor.net_id);
		if (!netId.IsValid())
			return;

		if (!m_netWorld)
			return;

		if (actor.op == fb::fbLifecycleOp_Remove)
		{
			if (actor.remove_reason == fb::fbRemovalReason_Destroyed)
			{
				m_netWorld->DestroyReplicatedActor(netId);
				m_replicas.erase(netId);

				if (m_localNetId == netId)
				{
					m_localNetId  = NetId::Invalid();
					m_localEntity = entt::null;
					ClearLocalActorRef();
				}
				return;
			}

			m_netWorld->SetReplicatedActorDormant(netId);
			return;
		}

		const auto* meta = actor.meta.get();
		if (!meta) return;

		entt::entity entity = entt::null;
		if (meta->spawn_req_id != 0)
		{
			if (meta->owner_user_id == m_userId)
			{
				entity = m_netWorld->TryConfirmPendingSpawn(netId, meta->spawn_req_id);
			}
		}

		if (entity == entt::null || !m_world.valid(entity))
		{
			const px::eBodyType bodyType = static_cast<px::eBodyType>(meta->body_type);
			if (bodyType == px::eBodyType::None)
				JAM_CRASH("[ProcessLifecycleActor] : NetActorBodyType is None.");

			entity = m_netWorld->EnsureReplicatedActor(netId, px::PrefabKey{ meta->prefab_key }, meta->owner_user_id, meta->controller_user_id, bodyType);
		}

		if (entity == entt::null || !m_world.valid(entity))
			return;

		if (!m_world.all_of<NetActorBodyType>(entity))
		{
			JAM_CRASH("[ProcessLifecycleActor] : Missing NetActorBodyType on replicated actor");
			return;
		}

		const px::eBodyType metaBodyType  = static_cast<px::eBodyType>(meta->body_type);
		const px::eBodyType actorBodyType = m_world.get<NetActorBodyType>(entity).body;
		if (actorBodyType != metaBodyType)
		{
			JAM_CRASH("[ProcessLifecycleActor] : Lifecycle body type mismatch on replicated actor");
			return;
		}

		Replica& replica = GetOrCreateReplica(netId);
		replica.e = entity;
		ApplyActorMeta(netId, entity, *meta, replica);

		const bool wasHidden = m_world.all_of<OutOfAoiTag>(entity) || m_world.all_of<PredictedDespawnTag>(entity);
		if (wasHidden)
			m_netWorld->ReactivateReplicatedActor(netId, replica.isLocal);
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

		const bool hasFull		= (ent.transform_full  != nullptr);
		const bool hasDelta		= (ent.transform_delta != nullptr);
		const bool hasKine		= (ent.kinematic_state != nullptr);
		const bool hasCharFull  = (ent.character_full  != nullptr);
		const bool hasCharDelta = (ent.character_delta != nullptr);

		const entt::entity resolved = ResolveEntityForSnapshot(nid);
		if (resolved == entt::null || !m_world.valid(resolved))
			return;

		Replica& replica = GetOrCreateReplica(nid);
		replica.e			 = resolved;
		replica.lastSeenTick = serverTick;

		const bool hiddenByAoi = m_world.all_of<OutOfAoiTag>(resolved);
		const bool hiddenByPrediction = m_world.all_of<PredictedDespawnTag>(resolved);
		const px::eBodyType snapshotBodyType = (hasCharFull || hasCharDelta) ? px::eBodyType::Character : px::eBodyType::Rigid;
		const NetActorBodyType* actorBody = nullptr;
		if (hasFull || hasDelta || hasKine || hasCharFull || hasCharDelta)
		{
			actorBody = m_world.try_get<NetActorBodyType>(resolved);
			if (!actorBody)
			{
				JAM_CRASH("[ProcessEntity] : Missing NetActorBodyType on replicated actor");
				return;
			}

			const px::eBodyType actorBodyType = actorBody->body;
			if (actorBodyType != snapshotBodyType)
			{
				JAM_CRASH("[ProcessEntity] : Snapshot body type mismatch on replicated actor");
				return;
			}
		}

		if (hasCharFull)
		{
			ApplyCharacterFullSnapshot(replica, serverTick, ent.character_full.get(), baselineRev, inputEpoch);
		}
		else if (hasCharDelta)
		{
			ApplyCharacterDeltaSnapshot(replica, serverTick, ent.character_delta.get(), baselineRev, inputEpoch);
		}
		else if (hasKine)
		{
			ApplyKinematicStateSnapshot(replica, serverTick, ent.kinematic_state.get(), baselineRev);
		}
		else if (hasFull)
		{
			ApplyRigidFullSnapshot(replica, serverTick, ent.transform_full.get(), baselineRev);
		}
		else if (hasDelta)
		{
			ApplyRigidDeltaSnapshot(replica, serverTick, ent.transform_delta.get(), baselineRev);
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

		if (hiddenByAoi)
			return;

		if (m_world.all_of<PhysicsSpawnedTag>(resolved))
			return;

		if (actorBody != nullptr && actorBody->body == px::eBodyType::Rigid)
		{
			if (const auto* auth = m_world.try_get<RigidAuthorityState>(resolved))
			{
				if (auth->state.kineType == px::eKineDrivenType::TargetDerived)
				{
					const auto* ti = m_world.try_get<TargetInfo>(resolved);
					if (!ti || ti->targetObjId == px::INVALID_OBJ_ID)
						return;
				}
			}
		}

		if (hiddenByPrediction)
		{
			if (m_netWorld)
				m_netWorld->ReactivateReplicatedActor(nid, replica.isLocal);
		}

		if (m_clientPhysics)
			m_clientPhysics->SpawnActor(resolved, replica.isLocal);
	}

	entt::entity ClientReplicationSystem::ResolveEntityForSnapshot(NetId netId)
	{
		if (!m_netWorld)
			return entt::null;

		return m_netWorld->GetEntity(netId);
	}

	void ClientReplicationSystem::ApplyRigidFullSnapshot(Replica& replica, uint64 serverTick, const fb::fbTransformFull* tf, uint32 baselineRev)
	{
		px::RigidState unpacked{};
		if (!UnpackRigidFull192(tf->data0(), tf->data1(), tf->data2(), unpacked))
			return;

		replica.lastSeenTick = serverTick;
		replica.baselineRev  = baselineRev;
		replica.baselinePos  = unpacked.pose.p;
		replica.baselineRot  = unpacked.pose.q;
		replica.hasBaseline  = true;

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyFullSnapshot] : Invalid entity");
			return;
		}

		auto& [rs] = m_world.get<RigidAuthorityState>(replica.e);
		rs = unpacked;
	}

	void ClientReplicationSystem::ApplyRigidDeltaSnapshot(Replica& replica, uint64 serverTick, const fb::fbTransformDelta* tf, uint32 baselineRev)
	{
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
	}

	void ClientReplicationSystem::ApplyKinematicStateSnapshot(Replica& replica, uint64 serverTick, const fb::fbKinematicState* ks, uint32 baselineRev)
	{
		if (!ks) return;

		replica.lastSeenTick = serverTick;
		replica.baselineRev	 = baselineRev;
		replica.hasBaseline  = false;

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyKinematicStateSnapshot] : Invalid entity");
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
			if (m_estimatedServerTick && m_estimatedServerTick->valid)
			{
				const double dtTick = m_estimatedServerTick->estimatedNowTick - static_cast<double>(serverTick);
				if (dtTick > 0.0)
					kine.t += static_cast<float>(dtTick * static_cast<double>(SIMULATION_TICK_SEC));
			}
		}

		auto& [rs] = m_world.get<RigidAuthorityState>(replica.e);
		rs.kineType  = kineType;
		rs.kineState = kine;
	}

	void ClientReplicationSystem::ApplyCharacterFullSnapshot(Replica& replica, uint64 serverTick, const fb::fbCharacterFull160* ch, uint32 baselineRev, uint32 inputEpoch)
	{
		px::CharacterState unpacked{};
		if (!UnpackCharacterFull160(ch->data0(), ch->data1(), ch->data2(), ch->data3(), ch->data4(), unpacked))
			return;

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

		if (replica.isLocal)
		{
			auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
			cs = unpacked;

			auto& signal = *m_reconcileSignal;
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

	void ClientReplicationSystem::ApplyCharacterDeltaSnapshot(Replica& replica, uint64 serverTick, const fb::fbCharacterDelta128* ch, uint32 baselineRev, uint32 inputEpoch)
	{
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

		if (replica.isLocal)
		{
			auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
			cs = unpacked;

			auto& signal = *m_reconcileSignal;
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
		const uint64 owner		= meta.owner_user_id;
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
		if (!m_clientPhysics) return;

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
			m_clientPhysics->SpawnActor(e, replica.isLocal);
		}
	}

	bool ClientReplicationSystem::TryResolveTargetObjId(NetId targetNetId, px::ObjectId& outObjId)
	{
		outObjId = px::INVALID_OBJ_ID;

		if (!targetNetId.IsValid())
			return false;

		if (!m_netWorld)
			return false;

		const entt::entity targetEntity = m_netWorld->GetEntity(targetNetId);
		if (targetEntity == entt::null || !m_world.valid(targetEntity))
			return false;

		outObjId = MakeObjectId(targetEntity);
		return true;
	}

	uint32 ClientReplicationSystem::GetCurrentLocalCommandEpoch() const
	{
		if (m_netWorld)
			return m_netWorld->GetLatestLocalCommandEpoch();

		return 0;
	}

	void ClientReplicationSystem::SetLocalActorRef(NetId netId, entt::entity entity)
	{
		if (m_localActorRef)
		{
			m_localActorRef->netId  = netId;
			m_localActorRef->entity = entity;
		}
	}

	void ClientReplicationSystem::ClearLocalActorRef()
	{
		if (m_localActorRef)
			m_localActorRef->Clear();
	}
}
