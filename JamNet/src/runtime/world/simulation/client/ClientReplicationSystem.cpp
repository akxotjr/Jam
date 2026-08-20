#include "pch.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/protocol/codec/ReplicationCodec.h"
#include "jamnet/runtime/world/simulation/client/ClientReplicationSystem.h"

#include "jamnet/runtime/world/simulation/client/ClientWorld.h"
#include "jamnet/runtime/world/simulation/client/ClientPhysicsSystem.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"

namespace jam::net
{
	ClientReplicationSystem::ClientReplicationSystem(entt::registry& world)
		: m_world(world)
	{
	}

	void ClientReplicationSystem::Init()
	{
		Clear();

		if (auto* nw = m_world.ctx().find<ClientWorld*>())
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
		m_deferredBaselineSnapshots.clear();
		m_pendingLifecycle.clear();
		m_pendingSnapshotBatches.clear();
		m_pendingBaselineFeedback.clear();
		m_headlessSnapshotBatch.reset();
		m_headlessBaselines.clear();
		m_localActorId = ActorId::Invalid();
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
		{
			FlushBaselineFeedback();
			return;
		}

		// Snapshot delivery on UNRELIABLE_SEQUENCED is freshness-biased and drop-tolerant.
		// Older incomplete ticks must never block the newest available tick.
		const uint64 latestQueuedTick = std::max(m_latestQueuedSnapshotTick, m_pendingSnapshotBatches.rbegin()->first);
		if (latestQueuedTick <= m_lastAppliedSnapshotTick)
		{
			for (const auto& item : m_pendingSnapshotBatches)
				PreserveDeferredBaselineSnapshots(item.second);
			m_pendingSnapshotBatches.clear();
			return;
		}

		for (auto it = m_pendingSnapshotBatches.begin(); it != m_pendingSnapshotBatches.end();)
		{
			if (it->first >= latestQueuedTick)
				break;
			PreserveDeferredBaselineSnapshots(it->second);
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
				ProcessEntity(*entPtr, batch.serverTick, batch.inputAck, batch.inputEpoch);
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
		FlushBaselineFeedback();
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
		{
			for (const auto& entPtr : snapshot.entities)
			{
				if (!entPtr) continue;
				StoreDeferredBaselineSnapshot(*entPtr, serverTick, hdr->input_ack, hdr->input_epoch);
			}
			return;
		}

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

	void ClientReplicationSystem::AcceptHeadlessSnapshot(const fb::fbSnapshot& snapshot)
	{
		const auto* header = snapshot.header();
		if (!header)
			return;

		const uint64 serverTick = header->server_tick();
		const uint16 chunkCount = std::max<uint16>(1, header->chunk_count());
		const uint16 chunkIndex = header->chunk_index();
		if (serverTick == 0 || chunkIndex >= chunkCount)
			return;

		if (!m_headlessSnapshotBatch || serverTick > m_headlessSnapshotBatch->serverTick)
		{
			m_headlessSnapshotBatch = HeadlessSnapshotBatch{
				.serverTick = serverTick,
				.expectedChunkCount = chunkCount,
				.receivedChunks = std::vector<uint8>(chunkCount, 0),
			};
		}
		else if (serverTick < m_headlessSnapshotBatch->serverTick)
		{
			return;
		}

		HeadlessSnapshotBatch& batch = *m_headlessSnapshotBatch;
		if (batch.expectedChunkCount != chunkCount || batch.receivedChunks[chunkIndex] != 0)
			return;

		if (const auto* entities = snapshot.entities())
		{
			for (const fb::fbActorEntity* entity : *entities)
			{
				if (!entity)
					continue;

				const ActorId actorId(entity->actor_id());
				const uint32 baselineRev = entity->baseline_rev();
				if (!actorId.IsValid() || baselineRev == 0)
					continue;

				if (entity->character_full() || entity->transform_full())
				{
					batch.baselines[actorId] = baselineRev;
					continue;
				}

				if (entity->character_delta() || entity->transform_delta())
				{
					const auto accepted = m_headlessBaselines.find(actorId);
					if (accepted == m_headlessBaselines.end() || accepted->second != baselineRev)
						QueueFullRequest(actorId, baselineRev);
				}
			}
		}

		batch.receivedChunks[chunkIndex] = 1;
		if (!batch.IsComplete())
			return;

		for (const auto& [actorId, baselineRev] : batch.baselines)
		{
			m_headlessBaselines[actorId] = baselineRev;
			QueueBaselineAck(actorId, baselineRev);
		}
		m_headlessSnapshotBatch.reset();
		FlushBaselineFeedback();
	}

	void ClientReplicationSystem::ProcessLifecycleActor(const fb::fbLifecycleActorT& actor)
	{
		const ActorId actorId = ActorId(actor.actor_id);
		if (!actorId.IsValid())
			return;

		if (!m_netWorld)
			return;

		if (actor.op == fb::fbLifecycleOp_Remove)
		{
			m_deferredBaselineSnapshots.erase(actorId);

			if (actor.remove_reason == fb::fbRemovalReason_Destroyed)
			{
				m_netWorld->DestroyReplicatedActor(actorId);
				m_replicas.erase(actorId);

				if (m_localActorId == actorId)
				{
					m_localActorId  = ActorId::Invalid();
					m_localEntity = entt::null;
					ClearLocalActorRef();
				}
				return;
			}

			m_netWorld->SetReplicatedActorDormant(actorId);
			return;
		}

		const auto* meta = actor.meta.get();
		if (!meta) return;

		bool created = false;
		const px::eBodyType bodyType = static_cast<px::eBodyType>(meta->body_type);
		if (bodyType == px::eBodyType::None)
			JAM_CRASH("[ProcessLifecycleActor] : ActorBodyType is None.");

		const entt::entity entity = m_netWorld->EnsureReplicatedActor(actorId, ActorArchetypeKey::FromU64(meta->actor_archetype_key), meta->owner_user_id, meta->controller_user_id, bodyType, &created);

		if (entity == entt::null || !m_world.valid(entity))
			return;

		if (!m_world.all_of<ActorBodyType>(entity))
		{
			JAM_CRASH("[ProcessLifecycleActor] : Missing ActorBodyType on replicated actor");
			return;
		}

		const px::eBodyType metaBodyType  = static_cast<px::eBodyType>(meta->body_type);
		const px::eBodyType actorBodyType = m_world.get<ActorBodyType>(entity).body;
		if (actorBodyType != metaBodyType)
		{
			JAM_CRASH("[ProcessLifecycleActor] : Lifecycle body type mismatch on replicated actor");
			return;
		}

		Replica& replica = GetOrCreateReplica(actorId);
		replica.e = entity;
		ApplyActorMeta(actorId, entity, *meta, replica);
		if (created)
			m_netWorld->PublishActorSpawned(entity, meta->client_request_id, replica.isLocal, eActorLifecycleReason::Spawned);

		const bool wasHidden = m_world.all_of<OutOfAoiTag>(entity) || m_world.all_of<LocallyHiddenTag>(entity);
		if (wasHidden)
			m_netWorld->ReactivateReplicatedActor(actorId, replica.isLocal);

		ApplyDeferredBaselineSnapshot(actorId);
	}

	void ClientReplicationSystem::ApplyActorMeta(ActorId actorId, entt::entity entity, const fb::fbActorMetaT& meta, Replica& replica)
	{
		if (entity == entt::null || !m_world.valid(entity))
			return;

		m_world.emplace_or_replace<ActorArchetypeRef>(entity, ActorArchetypeRef{ ActorArchetypeKey::FromU64(meta.actor_archetype_key) });
		if (px::PhysicsArchetypeKey physicsArchetypeKey{}; m_netWorld->TryResolvePhysicsArchetypeKey(ActorArchetypeKey::FromU64(meta.actor_archetype_key), physicsArchetypeKey))
			m_world.emplace_or_replace<PhysicsArchetypeRef>(entity, PhysicsArchetypeRef{ physicsArchetypeKey });
		else if (m_world.all_of<PhysicsArchetypeRef>(entity))
			m_world.remove<PhysicsArchetypeRef>(entity);
		m_world.emplace_or_replace<OwnershipTag>(entity, OwnershipTag{ meta.owner_user_id });
		m_world.emplace_or_replace<ControlTag>(entity, ControlTag{ meta.controller_user_id });
		replica.e = entity;
		UpdateUniqueLocalFromMeta(actorId, meta, replica);
	}

	void ClientReplicationSystem::ProcessEntity(const fb::fbActorEntityT& ent, uint64 serverTick, uint32 inputAck, uint32 inputEpoch)
	{
		const ActorId actorId = ActorId(ent.actor_id);
		if (!actorId.IsValid())
			return;

		const uint32 baselineRev = ent.baseline_rev;

		const bool hasFull		= (ent.transform_full  != nullptr);
		const bool hasDelta		= (ent.transform_delta != nullptr);
		const bool hasKine		= (ent.kinematic_state != nullptr);
		const bool hasCharFull  = (ent.character_full  != nullptr);
		const bool hasCharDelta = (ent.character_delta != nullptr);

		const entt::entity resolved = ResolveEntityForSnapshot(actorId);
		if (resolved == entt::null || !m_world.valid(resolved))
		{
			StoreDeferredBaselineSnapshot(ent, serverTick, inputAck, inputEpoch);
			return;
		}

		Replica& replica = GetOrCreateReplica(actorId);
		replica.e			 = resolved;
		replica.lastSeenTick = serverTick;

		const bool hiddenByAoi = m_world.all_of<OutOfAoiTag>(resolved);
		const bool hiddenLocally = m_world.all_of<LocallyHiddenTag>(resolved);
		const px::eBodyType snapshotBodyType = (hasCharFull || hasCharDelta) ? px::eBodyType::Character : px::eBodyType::Rigid;
		const ActorBodyType* actorBody = nullptr;
		if (hasFull || hasDelta || hasKine || hasCharFull || hasCharDelta)
		{
			actorBody = m_world.try_get<ActorBodyType>(resolved);
			if (!actorBody)
			{
				JAM_CRASH("[ProcessEntity] : Missing ActorBodyType on replicated actor");
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
			ApplyCharacterFullSnapshot(replica, serverTick, ent.character_full.get(), baselineRev, inputAck, inputEpoch);
			if (replica.hasBaseline && replica.baselineRev == baselineRev)
				QueueBaselineAck(actorId, baselineRev);
		}
		else if (hasCharDelta)
		{
			ApplyCharacterDeltaSnapshot(replica, serverTick, ent.character_delta.get(), baselineRev, inputAck, inputEpoch);
		}
		else if (hasKine)
		{
			ApplyKinematicStateSnapshot(replica, serverTick, ent.kinematic_state.get(), baselineRev);
		}
		else if (hasFull)
		{
			ApplyRigidFullSnapshot(replica, serverTick, ent.transform_full.get(), baselineRev);
			if (replica.hasBaseline && replica.baselineRev == baselineRev)
				QueueBaselineAck(actorId, baselineRev);
		}
		else if (hasDelta)
		{
			ApplyRigidDeltaSnapshot(replica, serverTick, ent.transform_delta.get(), baselineRev);
		}

		if (hasCharFull || hasFull)
			m_deferredBaselineSnapshots.erase(actorId);

		const bool hasResolvedSpawnState =
			hasCharFull
			|| hasFull
			|| hasKine
			|| ((hasCharDelta || hasDelta) && replica.hasBaseline);

		if (!hasResolvedSpawnState)
		{
			JAM_LOG_WARN("[ProcessEntity] actor id= {}, doesn't have resolved spawn state", actorId.Value());
			return;
		}

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
					if (!ti || ti->resolvedActorId == px::INVALID_ACTOR_ID)
						return;
				}
			}
		}

		if (hiddenLocally)
		{
			if (m_netWorld)
				m_netWorld->ReactivateReplicatedActor(actorId, replica.isLocal);
		}

		if (m_clientPhysics)
			m_clientPhysics->SpawnActor(resolved, replica.isLocal);
	}


	entt::entity ClientReplicationSystem::ResolveEntityForSnapshot(ActorId actorId)
	{
		if (!m_netWorld)
			return entt::null;

		return m_netWorld->ResolveActor(actorId);
	}

	void ClientReplicationSystem::PreserveDeferredBaselineSnapshots(const PendingSnapshotBatch& batch)
	{
		for (const auto& chunk : batch.chunks)
		{
			if (!chunk.has_value())
				continue;

			for (const auto& entPtr : chunk->snapshot.entities)
			{
				if (!entPtr) continue;
				StoreDeferredBaselineSnapshot(*entPtr, batch.serverTick, batch.inputAck, batch.inputEpoch);
			}
		}
	}

	void ClientReplicationSystem::StoreDeferredBaselineSnapshot(const fb::fbActorEntityT& ent, uint64 serverTick, uint32 inputAck, uint32 inputEpoch)
	{
		const ActorId actorId = ActorId(ent.actor_id);
		if (!actorId.IsValid())
			return;

		if (!HasBaselinePayload(ent))
			return;

		if (!NeedsBaseline(actorId))
			return;

		auto& pending = m_deferredBaselineSnapshots[actorId];
		if (pending.serverTick > serverTick)
			return;

		pending.entity = ent;
		pending.serverTick = serverTick;
		pending.inputAck = inputAck;
		pending.inputEpoch = inputEpoch;
	}

	void ClientReplicationSystem::ApplyDeferredBaselineSnapshot(ActorId actorId)
	{
		auto it = m_deferredBaselineSnapshots.find(actorId);
		if (it == m_deferredBaselineSnapshots.end())
			return;

		if (!NeedsBaseline(actorId))
		{
			m_deferredBaselineSnapshots.erase(it);
			return;
		}

		const entt::entity resolved = ResolveEntityForSnapshot(actorId);
		if (resolved == entt::null || !m_world.valid(resolved))
			return;

		DeferredBaselineSnapshot pending = std::move(it->second);
		m_deferredBaselineSnapshots.erase(it);
		ProcessEntity(pending.entity, pending.serverTick, pending.inputAck, pending.inputEpoch);
	}

	bool ClientReplicationSystem::HasBaselinePayload(const fb::fbActorEntityT& ent) const
	{
		return ent.character_full != nullptr || ent.transform_full != nullptr;
	}

	bool ClientReplicationSystem::NeedsBaseline(ActorId actorId) const
	{
		if (!actorId.IsValid())
			return false;

		const auto it = m_replicas.find(actorId);
		return it == m_replicas.end() || !it->second.hasBaseline;
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
		{
			QueueFullRequest(replica.actorId, baselineRev);
			return;
		}

		if (baselineRev != replica.baselineRev)
		{
			replica.hasBaseline = false;
			QueueFullRequest(replica.actorId, baselineRev);
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

		const ActorId targetActorId = ActorId(ks->target_id());
		kine.targetActorId = px::INVALID_ACTOR_ID;

		TargetInfo targetInfo{};
		targetInfo.targetActorId = targetActorId;
		targetInfo.resolvedActorId = px::INVALID_ACTOR_ID;

		if (targetActorId.IsValid())
		{
			px::ActorId resolved = px::INVALID_ACTOR_ID;
			if (TryResolveTargetActorId(targetActorId, resolved))
			{
				kine.targetActorId = resolved;
				targetInfo.resolvedActorId = resolved;
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

	void ClientReplicationSystem::ApplyCharacterFullSnapshot(Replica& replica, uint64 serverTick, const fb::fbCharacterFull160* ch, uint32 baselineRev, uint32 inputAck, uint32 inputEpoch)
	{
		px::CharacterState unpacked{};
		if (!UnpackCharacterFull160(ch->data0(), ch->data1(), ch->data2(), ch->data3(), ch->data4(), unpacked))
			return;

		replica.lastSeenTick	= serverTick;
		replica.baselineRev		= baselineRev;
		replica.baselinePos		= unpacked.pos;
		replica.baselineBodyYaw	= unpacked.bodyYaw;
		replica.baselineViewYaw	= unpacked.viewYaw;
		replica.baselinePitch	= unpacked.viewPitch;
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
			const uint32 currentControlRevision = GetCurrentLocalControlRevision();
			if (inputAck > signal.inputAck && inputEpoch >= currentControlRevision)
			{
				signal.serverTick = serverTick;
				signal.inputAck   = inputAck;
				signal.dirty	  = true;
			}

			auto& history = m_world.get<CharReplayHistory>(replica.e);
			history.Push(serverTick, unpacked);
			return;
		}

		auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
		cs = unpacked;
	}

	void ClientReplicationSystem::ApplyCharacterDeltaSnapshot(Replica& replica, uint64 serverTick, const fb::fbCharacterDelta128* ch, uint32 baselineRev, uint32 inputAck, uint32 inputEpoch)
	{
		replica.lastSeenTick = serverTick;

		if (replica.e == entt::null || !m_world.valid(replica.e))
		{
			JAM_CRASH("[ApplyCharacterDeltaSnapshot] : Invalid entity");
			return;
		}

		if (!replica.hasBaseline)
		{
			JAM_LOG_WARN("[ApplyCharacterDeltaSnapshot] actor id= {}, replica doesn't have baseline", replica.actorId.Value());
			QueueFullRequest(replica.actorId, baselineRev);
			return;
		}

		if (baselineRev != replica.baselineRev)
		{
			replica.hasBaseline = false;
			QueueFullRequest(replica.actorId, baselineRev);
			return;
		}

		px::CharacterState unpacked{};
		if (!UnpackCharacterDelta128(replica.baselinePos, replica.baselineBodyYaw, replica.baselineViewYaw, replica.baselinePitch, ch->data0(), ch->data1(), unpacked))
			return;

		if (replica.isLocal)
		{
			auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
			cs = unpacked;

			auto& signal = *m_reconcileSignal;
			const uint32 currentControlRevision = GetCurrentLocalControlRevision();
			if (inputAck > signal.inputAck && inputEpoch >= currentControlRevision)
			{
				signal.serverTick = serverTick;
				signal.inputAck   = inputAck;
				signal.dirty	  = true;
			}

			auto& history = m_world.get<CharReplayHistory>(replica.e);
			history.Push(serverTick, unpacked);
			return;
		}

		auto& [cs] = m_world.get<CharAuthorityState>(replica.e);
		cs = unpacked;
	}

	void ClientReplicationSystem::QueueBaselineAck(ActorId actorId, uint32 baselineRev)
	{
		if (!actorId.IsValid() || baselineRev == 0)
			return;

		auto& feedback = m_pendingBaselineFeedback[actorId];
		if (!feedback.requestFull || baselineRev >= feedback.baselineRev)
			feedback = PendingBaselineFeedback{ .baselineRev = baselineRev, .requestFull = false };
	}

	void ClientReplicationSystem::QueueFullRequest(ActorId actorId, uint32 baselineRev)
	{
		if (!actorId.IsValid())
			return;

		m_pendingBaselineFeedback[actorId] = PendingBaselineFeedback
		{
			.baselineRev = baselineRev,
			.requestFull = true,
		};
	}

	void ClientReplicationSystem::FlushBaselineFeedback()
	{
		if (!m_netWorld || m_pendingBaselineFeedback.empty())
			return;

		const WorldRef world = m_netWorld->GetWorldRef();
		if (!world.IsValid())
			return;

		constexpr size_t kBaselineFeedbackBatchSize = 96;

		std::vector<std::pair<ActorId, PendingBaselineFeedback>> pending;
		pending.reserve(m_pendingBaselineFeedback.size());
		for (const auto& [actorId, feedback] : m_pendingBaselineFeedback)
			pending.emplace_back(actorId, feedback);

		auto buildPayload = [&](size_t offset, size_t count, flatbuffers::FlatBufferBuilder& fbb)
			{
				std::vector<fb::fbBaselineAck> entries;
				entries.reserve(count);
				for (size_t i = 0; i < count; ++i)
				{
					const auto& [actorId, feedback] = pending[offset + i];
					entries.emplace_back(actorId.Value(), feedback.baselineRev, feedback.requestFull);
				}

				const auto entryVector = fbb.CreateVectorOfStructs(entries);
				const auto batch = fb::CreatefbBaselineAckBatch(fbb, world.worldId, world.instance.instanceId.value, entryVector);
				fbb.Finish(batch, fb::fbBaselineAckBatchIdentifier());
			};

		for (size_t offset = 0; offset < pending.size();)
		{
			const size_t batchCount = std::min(kBaselineFeedbackBatchSize, pending.size() - offset);

			flatbuffers::FlatBufferBuilder fbb;
			buildPayload(offset, batchCount, fbb);
			if (fbb.GetSize() > MAX_PAYLOAD_SIZE)
			{
				JAM_LOG_WARN("[BaselineFeedback] ACK batch exceeds MTU payload budget. entries={}, payloadSize={}, maxPayloadSize={}",
					batchCount, fbb.GetSize(), MAX_PAYLOAD_SIZE);
				return;
			}

			auto packet = PacketBuilder::CreateCustomPacket(
				CustomPacketId::BASELINE_ACK,
				PacketFlags::NONE,
				eChannel::RELIABLE_ORDERED,
				fbb.GetBufferPointer(),
				fbb.GetSize());
			if (!packet.IsValid())
				return;

			m_netWorld->Send(std::move(packet));
			for (size_t i = 0; i < batchCount; ++i)
				m_pendingBaselineFeedback.erase(pending[offset + i].first);
			offset += batchCount;
		}
	}

	Replica& ClientReplicationSystem::GetOrCreateReplica(ActorId actorId, bool* created)
	{
		auto it = m_replicas.find(actorId);
		if (it != m_replicas.end())
		{
			if (created) *created = false;
			return it->second;
		}

		Replica rep{};
		rep.actorId = actorId;
		auto [iter, _] = m_replicas.emplace(actorId, rep);
		if (created) *created = true;
		return iter->second;
	}

	void ClientReplicationSystem::PruneOldReplicas(uint64 serverTick, uint64 forgetAfterTicks)
	{
		std::vector<ActorId> toErase;
		for (const auto& [id, replica] : m_replicas)
		{
			if (serverTick > replica.lastSeenTick && (serverTick - replica.lastSeenTick) > forgetAfterTicks)
				toErase.push_back(id);
		}

		for (ActorId id : toErase)
			m_replicas.erase(id);

		toErase.clear();
		for (const auto& [id, pending] : m_deferredBaselineSnapshots)
		{
			if (serverTick > pending.serverTick && (serverTick - pending.serverTick) > forgetAfterTicks)
				toErase.push_back(id);
		}

		for (ActorId id : toErase)
			m_deferredBaselineSnapshots.erase(id);
	}

	void ClientReplicationSystem::UpdateUniqueLocalFromMeta(ActorId actorId, const fb::fbActorMetaT& meta, Replica& replica)
	{
		const uint64 owner		= meta.owner_user_id;
		const uint64 controller = meta.controller_user_id;

		const bool isLocalCandidate = (owner != 0 && owner == m_userId && controller == m_userId);
		const bool wasLocal = (m_localActorId == actorId && m_localEntity == replica.e);

		if (!isLocalCandidate)
		{
			replica.isLocal = false;

			if (wasLocal)
			{
				m_localActorId  = ActorId::Invalid();
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

		if (m_localActorId == actorId && m_localEntity == replica.e)
		{
			replica.isLocal = true;
			SetLocalActorRef(actorId, replica.e);
			if (replica.e != entt::null && m_world.valid(replica.e))
			{
				m_world.remove<RemoteActorTag>(replica.e);
				m_world.emplace_or_replace<LocalActorTag>(replica.e);
			}
			return;
		}

		if (m_localActorId.IsValid())
		{
			if (auto it = m_replicas.find(m_localActorId); it != m_replicas.end())
			{
				it->second.isLocal = false;
				if (it->second.e != entt::null && m_world.valid(it->second.e))
				{
					m_world.remove<LocalActorTag>(it->second.e);
					m_world.emplace_or_replace<RemoteActorTag>(it->second.e);
				}
			}
		}

		m_localActorId  = actorId;
		m_localEntity = replica.e;
		SetLocalActorRef(actorId, replica.e);

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

		auto view = m_world.view<ActorId, ActorBodyType, RigidAuthorityState, TargetInfo>(entt::exclude<PhysicsSpawnedTag, LocallyHiddenTag>);

		for (auto e : view)
		{
			const auto bodyType = view.get<ActorBodyType>(e).body;
			if (bodyType != px::eBodyType::Rigid)
				continue;

			const auto& auth = view.get<RigidAuthorityState>(e).state;
			if (auth.kineType != px::eKineDrivenType::TargetDerived)
				continue;

			auto& ti = view.get<TargetInfo>(e);
			if (!ti.targetActorId.IsValid())
				continue;

			if (ti.resolvedActorId == px::INVALID_ACTOR_ID)
			{
				px::ActorId resolved = px::INVALID_ACTOR_ID;
				if (!TryResolveTargetActorId(ti.targetActorId, resolved))
					continue;

				ti.resolvedActorId = resolved;
			}

			const ActorId actorId = view.get<ActorId>(e);
			Replica& replica = GetOrCreateReplica(actorId);
			m_clientPhysics->SpawnActor(e, replica.isLocal);
		}
	}

	bool ClientReplicationSystem::TryResolveTargetActorId(ActorId targetActorId, px::ActorId& outActorId)
	{
		outActorId = px::INVALID_ACTOR_ID;

		if (!targetActorId.IsValid())
			return false;

		if (!m_netWorld)
			return false;

		const entt::entity targetEntity = m_netWorld->ResolveActor(targetActorId);
		if (targetEntity == entt::null || !m_world.valid(targetEntity))
			return false;

		outActorId = GetPhysicsActorId(m_world, targetEntity);
		return true;
	}

	uint32 ClientReplicationSystem::GetCurrentLocalControlRevision() const
	{
		if (const auto* history = m_world.ctx().find<InputHistoryBuffer>())
			return history->current.intent.controlRevision;

		return 0;
	}

	void ClientReplicationSystem::SetLocalActorRef(ActorId actorId, entt::entity entity)
	{
		if (m_localActorRef)
		{
			m_localActorRef->actorId  = actorId;
			m_localActorRef->entity = entity;
		}
	}

	void ClientReplicationSystem::ClearLocalActorRef()
	{
		if (m_localActorRef)
			m_localActorRef->Clear();
	}
}
