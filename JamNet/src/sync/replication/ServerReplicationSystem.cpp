#include "pch.h"
#include "jamnet/sync/replication/ServerReplicationSystem.h"

#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationCodec.h"
#include "jamnet/sync/replication/ServerPhysicsSystem.h"
#include "jamnet/sync/replication/ServerInputSystem.h"

#include "jamnet/sync/networld/ServerPhysicalWorld.h"
#include "jamnet/sync/replication/WorldContext.h"
#include "jamnet/sync/replication/ServerAoiSystem.h"

#include "jamnet/sync/transport/CustomPacketHelper.h"

namespace jam::net
{
	namespace
	{
		constexpr size_t kLifecycleBatch = 96;
		constexpr size_t kSnapshotBatch = 128;
		constexpr size_t kPacketPayloadBudget = JAMNET_MTU - PacketHeader::HALF_SIZE - 24;
		constexpr uint8 kCreateFullStateBudget = 90;

		struct PlannedSnapshotCandidate
		{
			entt::entity e = entt::null;
			NetId netId = NetId::Invalid();
			bool useFull = false;
		};

		struct PlannedSnapshotChunk
		{
			std::vector<PlannedSnapshotCandidate> candidates;
		};
	}

	ServerReplicationSystem::ServerReplicationSystem(entt::registry& world)
		: m_world(world)
	{
		m_fbb.reset(new flatbuffers::FlatBufferBuilder(JAMNET_MTU));
	}

	void ServerReplicationSystem::Init()
	{
		m_tickCounter	 = 0;
		m_netWorld = nullptr;
		if (auto* nw = m_world.ctx().find<ServerPhysicalWorld*>())
			m_netWorld = (nw && *nw) ? *nw : nullptr;
		m_inputSys  = m_world.ctx().find<ServerInputSystem>();
		m_aoiSys	= m_world.ctx().find<ServerAoiSystem>();
		m_physSys	= m_world.ctx().find<ServerPhysicsSystem>();

		m_userStates.clear();
		m_knownUsersByActor.clear();
		m_forceLifecycleSyncPerUsers.clear();
		m_sharedRigidStates.clear();
		m_sharedCharacterStates.clear();
		m_actorFrameCache.clear();
		m_actorFrameNetIds.clear();
		m_usersScratch.clear();
		m_knownUsersScratch.clear();
		m_sentThisTickScratch.clear();
		m_enteredScratch.clear();
		for (auto& bucket : m_candidateBucketsScratch)
			bucket.clear();
		m_orderedCandidatesScratch.clear();
		m_actorOffsScratch.clear();
		m_dirtyActorFrameScratch.clear();
		m_dirtyActorFrameDedup.clear();
		m_prevActiveActors.clear();
		m_currentActiveActorsScratch.clear();

		auto frameSeedView = m_world.view<NetId, NetActorBodyType>();
		for (auto e : frameSeedView)
			MarkActorFrameDirty(e);

		m_fullCacheTick = 0;
		m_cachedRigidFull.clear();
		m_cachedCharacterFull.clear();
	}

	void ServerReplicationSystem::Tick()
	{
		++m_tickCounter;
		CaptureSnapshot();
	}

	void ServerReplicationSystem::CaptureSnapshot()
	{
		if (!m_netWorld || !m_aoiSys) return;

		if (m_fullCacheTick != m_tickCounter)
		{
			m_fullCacheTick = m_tickCounter;
			m_cachedRigidFull.clear();
			m_cachedCharacterFull.clear();
		}

		RefreshActorFrameCache();

		const bool periodicFull = ((m_tickCounter % kFullIntervalTicks) == 0);
		const uint32 tick = m_world.ctx().get<TickCounter>().tick;

		m_usersScratch.clear();
		m_netWorld->GetMembers(m_usersScratch);
		if (m_usersScratch.empty()) return;

		auto* inputSys = m_inputSys;

		for (const uint64 user : m_usersScratch)
		{
			auto& userState = m_userStates[user];
			const uint32 ack		= inputSys ? inputSys->LastAppliedSeq(user) : 0;
			const uint32 inputEpoch = inputSys ? inputSys->LastAppliedCommandEpoch(user) : 0;
			uint32 snapshotPacketCount = 0;
			uint32 snapshotActorCount = 0;
			uint32 visibleActorCount = 0;

			m_sentThisTickScratch.clear();
			m_sentThisTickScratch.reserve(256);

			bool forceSyncUser = false;
			if (auto it = m_forceLifecycleSyncPerUsers.find(user); it != m_forceLifecycleSyncPerUsers.end() && it->second > 0)
				forceSyncUser = true;

			m_enteredScratch.clear();

			if (const UserAoiState* st = m_aoiSys->GetState(user))
			{
				m_enteredScratch.reserve(st->entered.size());
				for (const NetId id : st->entered)
					m_enteredScratch.insert(id.Raw());
			}

			QueueLifecycleForVisibleActors(user, forceSyncUser);
			//QueueLifecycleForStaticActors(user, forceSyncUser);
			EmitPendingLifecyclePackets(user, tick);

			if (forceSyncUser)
			{
				auto it = m_forceLifecycleSyncPerUsers.find(user);
				if (it != m_forceLifecycleSyncPerUsers.end())
				{
					if (it->second > 0)
						--it->second;
					if (it->second <= 0)
						m_forceLifecycleSyncPerUsers.erase(it);
				}
			}

			for (auto& bucket : m_candidateBucketsScratch)
				bucket.clear();

			auto addCandidate = [&](entt::entity e, NetId nid)
			{
				const ActorFrameCache* actorFrame = FindActorFrameCache(nid);
				if (!actorFrame || actorFrame->e == entt::null || actorFrame->isStatic || !actorFrame->canReplicate)
					return;

				const auto   knownEpochIt	 = userState.knownFullEpoch.find(nid);
				const uint32 actorEpoch		 = actorFrame->fullEpoch;
				const bool   baselineInvalid = ShouldForceFullState(user, nid)
					|| (knownEpochIt == userState.knownFullEpoch.end())
					|| (knownEpochIt->second != actorEpoch)
					|| (actorEpoch == 0);

				const bool enteredNow = m_enteredScratch.contains(nid.Raw());

				Candidate c{ .e = actorFrame->e, .netId = nid };

				if (baselineInvalid || enteredNow)
				{
					c.useFull = true;
					m_candidateBucketsScratch[static_cast<size_t>(eBucket::B0_MustSendFull)].push_back(c);
					return;
				}

				if (periodicFull)
				{
					c.useFull = true;
					m_candidateBucketsScratch[static_cast<size_t>(eBucket::B0_MustSendFull)].push_back(c);
					return;
				}

				if (!actorFrame->isActive)
				{
					m_candidateBucketsScratch[static_cast<size_t>(eBucket::B3_LowPriority)].push_back(c);
					return;
				}

				if (actorFrame->bodyType == px::eBodyType::Character)
					m_candidateBucketsScratch[static_cast<size_t>(eBucket::B1_HighDelta)].push_back(c);
				else
					m_candidateBucketsScratch[static_cast<size_t>(eBucket::B2_NormalDelta)].push_back(c);
			};


			if (const auto* visibleActors = m_aoiSys->GetVisibleActors(user))
			{
				visibleActorCount = static_cast<uint32>(visibleActors->size());
				for (const AoiVisibleActorSlot& slot : *visibleActors)
				{
					if (!slot.alive || slot.actor == entt::null || !m_world.valid(slot.actor) || !m_world.all_of<NetId>(slot.actor))
						continue;
					addCandidate(slot.actor, m_world.get<NetId>(slot.actor));
				}
			}

			m_orderedCandidatesScratch.clear();
			m_orderedCandidatesScratch.reserve(m_candidateBucketsScratch[0].size() + m_candidateBucketsScratch[1].size() + m_candidateBucketsScratch[2].size() + m_candidateBucketsScratch[3].size());

			auto appendAll = [&](eBucket bucket)
			{
				auto& src = m_candidateBucketsScratch[static_cast<size_t>(bucket)];
				m_orderedCandidatesScratch.insert(m_orderedCandidatesScratch.end(), src.begin(), src.end());
			};

			appendAll(eBucket::B0_MustSendFull);
			appendAll(eBucket::B1_HighDelta);
			appendAll(eBucket::B2_NormalDelta);
			appendAll(eBucket::B3_LowPriority);

			if (m_orderedCandidatesScratch.empty())
				continue;

			auto estimateActorBytes = [&](const Candidate& c) -> size_t
			{
				if (const ActorFrameCache* actorFrame = FindActorFrameCache(c.netId))
					return c.useFull ? actorFrame->fullSizeEstimate : actorFrame->deltaSizeEstimate;
				return 0;
			};

			std::vector<PlannedSnapshotChunk> plannedChunks;
			size_t cursor = 0;
			while (cursor < m_orderedCandidatesScratch.size())
			{
				PlannedSnapshotChunk chunkPlan{};
				chunkPlan.candidates.reserve(kSnapshotBatch);
				size_t usedPayloadBudget = 0;
				const size_t beginCursor = cursor;

				for (; cursor < m_orderedCandidatesScratch.size(); ++cursor)
				{
					const Candidate& c = m_orderedCandidatesScratch[cursor];

					if (chunkPlan.candidates.size() >= kSnapshotBatch)
						break;

					const size_t est = estimateActorBytes(c);
					if (est > kPacketPayloadBudget)
					{
						JAMNET_LOG_WARN("[Snapshot] single actor too large. user={}, netId={}", user, c.netId.Raw());
						continue;
					}

					if ((usedPayloadBudget + est) > kPacketPayloadBudget)
					{
						if (!chunkPlan.candidates.empty())
							break;
						continue;
					}

					if (m_sentThisTickScratch.contains(c.netId))
						continue;

					m_sentThisTickScratch.insert(c.netId);
					chunkPlan.candidates.push_back(PlannedSnapshotCandidate{
						.e = c.e,
						.netId = c.netId,
						.useFull = c.useFull
					});
					usedPayloadBudget += est;
				}

				if (chunkPlan.candidates.empty())
				{
					if (cursor == beginCursor)
						++cursor;
					continue;
				}

				plannedChunks.push_back(std::move(chunkPlan));
			}

			const uint16 chunkCount = static_cast<uint16>(std::min<size_t>(plannedChunks.size(), std::numeric_limits<uint16>::max()));
			for (uint16 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
			{
				m_fbb->Clear();
				m_actorOffsScratch.clear();
				m_actorOffsScratch.reserve(plannedChunks[chunkIndex].candidates.size());

				for (const PlannedSnapshotCandidate& c : plannedChunks[chunkIndex].candidates)
				{
					flatbuffers::Offset<fb::fbActorEntity> off = 0;
					if (c.useFull)
						off = BuildFullActorEntity(c.e, user);
					else
						off = BuildDeltaActorEntity(c.e, user);

					if (off.IsNull())
						continue;

					m_actorOffsScratch.push_back(off);
					if (c.useFull)
						MarkFullStateSent(user, c.netId);
				}

				if (m_actorOffsScratch.empty())
					continue;

				const auto header = fb::CreatefbSnapshotHeader(*m_fbb, tick, ack, inputEpoch, chunkIndex, chunkCount);
				const auto vec	  = m_fbb->CreateVector(m_actorOffsScratch);
				const auto snap	  = fb::CreatefbSnapshot(*m_fbb, m_netWorld->GetWorldId(), header, vec);
				m_fbb->Finish(snap, fb::fbSnapshotIdentifier());

				auto pkt = PacketBuilder::CreateCustomPacket(CustomPacketId::SNAPSHOT, PacketFlags::NONE, eChannel::UNRELIABLE_SEQUENCED, m_fbb->GetBufferPointer(), m_fbb->GetSize());
				if (!pkt.IsValid())
					continue;

				m_netWorld->SendTo(pkt, user);
				++snapshotPacketCount;
				snapshotActorCount += static_cast<uint32>(m_actorOffsScratch.size());
			}

		}
	}

	void ServerReplicationSystem::ForceLifecycleSyncForUser(uint64 userId, int32 budget)
	{
		if (userId == 0 || budget <= 0)
			return;

		auto& slot = m_forceLifecycleSyncPerUsers[userId];
		slot = std::max(slot, budget);
	}

	const ReplicationUserState* ServerReplicationSystem::FindUserState(uint64 userId) const
	{
		if (const auto it = m_userStates.find(userId); it != m_userStates.end())
			return &it->second;
		return nullptr;
	}

	ReplicationUserState* ServerReplicationSystem::FindUserState(uint64 userId)
	{
		if (auto it = m_userStates.find(userId); it != m_userStates.end())
			return &it->second;
		return nullptr;
	}


	uint32 ServerReplicationSystem::GetActorFullEpoch(entt::entity e, NetId netId)
	{
		if (e == entt::null || !m_world.valid(e))
			return 0;

		const auto* body = m_world.try_get<NetActorBodyType>(e);
		if (!body)
			return 0;

		if (body->body == px::eBodyType::Character)
		{
			if (const auto it = m_sharedCharacterStates.find(netId); it != m_sharedCharacterStates.end())
				return it->second.fullEpoch;
			return 0;
		}

		if (body->body == px::eBodyType::Rigid)
		{
			if (const auto it = m_sharedRigidStates.find(netId); it != m_sharedRigidStates.end())
				return it->second.fullEpoch;
			return 0;
		}

		return 0;
	}

	void ServerReplicationSystem::RefreshActorFrameCache()
	{
		m_currentActiveActorsScratch.clear();
		if (m_physSys)
		{
			for (const entt::entity e : m_physSys->GetLastActiveEntities())
				m_currentActiveActorsScratch.insert(e);
		}

		for (const entt::entity e : m_prevActiveActors)
		{
			if (!m_currentActiveActorsScratch.contains(e))
				MarkActorFrameDirty(e);
		}

		for (const entt::entity e : m_currentActiveActorsScratch)
			MarkActorFrameDirty(e);

		for (const entt::entity e : m_dirtyActorFrameScratch)
			UpsertActorFrameCache(e, m_currentActiveActorsScratch.contains(e));

		m_dirtyActorFrameScratch.clear();
		m_dirtyActorFrameDedup.clear();
		m_prevActiveActors = m_currentActiveActorsScratch;
	}

	const ServerReplicationSystem::ActorFrameCache* ServerReplicationSystem::FindActorFrameCache(NetId netId) const
	{
		if (const auto it = m_actorFrameCache.find(netId); it != m_actorFrameCache.end())
			return &it->second;
		return nullptr;
	}

	void ServerReplicationSystem::AddKnownUserToActor(NetId netId, uint64 userId)
	{
		auto& slots = m_knownUsersByActor[netId];
		for (KnownUserSlot& slot : slots)
		{
			if (slot.alive && slot.userId == userId)
				return;
		}

		for (KnownUserSlot& slot : slots)
		{
			if (!slot.alive)
			{
				slot.userId = userId;
				slot.alive = true;
				return;
			}
		}

		slots.push_back(KnownUserSlot{ userId, true });
	}

	void ServerReplicationSystem::RemoveKnownUserFromActor(NetId netId, uint64 userId)
	{
		auto it = m_knownUsersByActor.find(netId);
		if (it == m_knownUsersByActor.end())
			return;

		for (KnownUserSlot& slot : it->second)
		{
			if (slot.alive && slot.userId == userId)
			{
				slot.alive = false;
				break;
			}
		}

		CompactKnownUsersIfNeeded(netId);
	}

	void ServerReplicationSystem::CompactKnownUsersIfNeeded(NetId netId)
	{
		auto it = m_knownUsersByActor.find(netId);
		if (it == m_knownUsersByActor.end())
			return;

		auto& slots = it->second;
		const size_t deadCount = std::count_if(slots.begin(), slots.end(), [](const KnownUserSlot& slot)
		{
			return !slot.alive;
		});

		if (deadCount == 0)
			return;

		if (deadCount != slots.size() && deadCount * 3 < slots.size())
			return;

		std::erase_if(slots, [](const KnownUserSlot& slot) { return !slot.alive; });
		if (slots.empty())
			m_knownUsersByActor.erase(it);
	}

	void ServerReplicationSystem::MarkActorFrameDirty(entt::entity e)
	{
		if (e == entt::null)
			return;

		if (m_dirtyActorFrameDedup.insert(e).second)
			m_dirtyActorFrameScratch.push_back(e);
	}

	void ServerReplicationSystem::UpsertActorFrameCache(entt::entity e, bool isActiveOverride)
	{
		const auto prevNetIdIt = m_actorFrameNetIds.find(e);
		if (e == entt::null || !m_world.valid(e) || !m_world.all_of<NetId, NetActorBodyType>(e))
		{
			if (prevNetIdIt != m_actorFrameNetIds.end())
			{
				m_actorFrameCache.erase(prevNetIdIt->second);
				m_actorFrameNetIds.erase(prevNetIdIt);
			}
			return;
		}

		const NetId netId = m_world.get<NetId>(e);
		const px::eBodyType bodyType = m_world.get<NetActorBodyType>(e).body;
		if (!netId.IsValid())
		{
			if (prevNetIdIt != m_actorFrameNetIds.end())
			{
				m_actorFrameCache.erase(prevNetIdIt->second);
				m_actorFrameNetIds.erase(prevNetIdIt);
			}
			return;
		}

		if (prevNetIdIt != m_actorFrameNetIds.end() && prevNetIdIt->second != netId)
			m_actorFrameCache.erase(prevNetIdIt->second);

		ActorFrameCache frame{};
		frame.e = e;
		frame.netId = netId;
		frame.fullEpoch = GetActorFullEpoch(e, netId);
		frame.bodyType = bodyType;
		frame.isStatic = m_world.all_of<ReplicationStaticTag>(e);
		frame.isActive = isActiveOverride;

		if (!frame.isStatic)
		{
			if (bodyType == px::eBodyType::Character)
			{
				frame.canReplicate = m_world.all_of<CharAuthorityState>(e);
				frame.fullSizeEstimate = 48;
				frame.deltaSizeEstimate = 40;
			}
			else if (bodyType == px::eBodyType::Rigid)
			{
				frame.canReplicate = m_world.all_of<RigidAuthorityState>(e);
				if (frame.canReplicate)
				{
					const auto& rs = m_world.get<RigidAuthorityState>(e);
					frame.isKinematic = (rs.state.kineType != px::eKineDrivenType::None
						&& rs.state.kineType != px::eKineDrivenType::RuntimeDynamic);
				}
				frame.fullSizeEstimate = frame.isKinematic ? 56 : 48;
				frame.deltaSizeEstimate = frame.isKinematic ? 56 : 40;
			}
		}

		m_actorFrameCache[netId] = frame;
		m_actorFrameNetIds[e] = netId;
	}

	void ServerReplicationSystem::MarkActorDirty(entt::entity e, bool forceMeta)
	{
		if (!m_world.valid(e) || !m_world.all_of<NetId>(e))
			return;

		const NetId netId = m_world.get<NetId>(e);
		if (!netId.IsValid())
			return;

		MarkActorFrameDirty(e);

		if (!forceMeta)
			return;

		auto* nw = m_netWorld;
		if (!nw)
			return;

		if (auto it = m_knownUsersByActor.find(netId); it != m_knownUsersByActor.end())
		{
			m_knownUsersScratch.clear();
			m_knownUsersScratch.reserve(it->second.size());
			for (const KnownUserSlot& slot : it->second)
			{
				if (slot.alive)
					m_knownUsersScratch.push_back(slot.userId);
			}

			for (const uint64 userId : m_knownUsersScratch)
				QueueLifecycleMetaForUser(userId, netId);
		}
	}

	void ServerReplicationSystem::OnUserEnter(uint64 userId)
	{
		if (userId == 0)
			return;

		JAMNET_LOG_DEBUG("[ServerReplicationSystem] OnEnter() : userId= {}", userId);
		ForceLifecycleSyncForUser(userId, 30);
	}

	void ServerReplicationSystem::OnUserLeave(uint64 userId)
	{
		if (userId == 0)
			return;

		if (auto* userState = FindUserState(userId))
		{
			for (const NetId netId : userState->knownActors)
				RemoveKnownUserFromActor(netId, userId);
		}

		m_userStates.erase(userId);
		m_forceLifecycleSyncPerUsers.erase(userId);
	}

	void ServerReplicationSystem::OnActorDestroyed(entt::entity e)
	{
		if (!m_world.valid(e) || !m_world.all_of<NetId>(e))
			return;

		const NetId netId = m_world.get<NetId>(e);
		if (!netId.IsValid())
			return;

		InvalidateAllUserCaches(netId);

		if (auto it = m_knownUsersByActor.find(netId); it != m_knownUsersByActor.end())
		{
			m_knownUsersScratch.clear();
			m_knownUsersScratch.reserve(it->second.size());
			for (const KnownUserSlot& slot : it->second)
			{
				if (slot.alive)
					m_knownUsersScratch.push_back(slot.userId);
			}

			for (const uint64 userId : m_knownUsersScratch)
				QueueRemovalForUser(userId, netId, fb::fbRemovalReason_Destroyed);
		}

		ForgetActorForAllUsers(netId);
		m_actorFrameCache.erase(netId);
		m_actorFrameNetIds.erase(e);
		m_prevActiveActors.erase(e);
		m_currentActiveActorsScratch.erase(e);
		m_dirtyActorFrameDedup.erase(e);
		m_sharedRigidStates.erase(netId);
		m_sharedCharacterStates.erase(netId);
		m_cachedRigidFull.erase(netId);
		m_cachedCharacterFull.erase(netId);
	}

	flatbuffers::Offset<fb::fbActorMeta> ServerReplicationSystem::BuildActorMeta(entt::entity e, uint64 userId)
	{
		uint64 owner		= 0;
		uint64 controller	= 0;
		uint64 prefab		= 0;
		uint32 spawnReqId	= 0;
		uint32 packedId		= 0;
		const auto* body = m_world.try_get<NetActorBodyType>(e);
		if (!body || body->body == px::eBodyType::None)
			return 0;

		const uint8 bodyType = static_cast<uint8>(body->body);

		if (auto* o   = m_world.try_get<OwnershipTag>(e))		owner		= o->userId;
		if (auto* c   = m_world.try_get<ControlTag>(e))			controller  = c->userId;
		if (auto* p   = m_world.try_get<NetPrefabKey>(e))		prefab		= p->key.value;
		if (auto* tpr = m_world.try_get<NetTeamPartRole>(e))	packedId	= tpr->Packed();

		if (userId != 0 && userId == owner)
		{
			if (auto* s = m_world.try_get<NetSpawnRequestId>(e))
				spawnReqId = s->requestId;
		}

		return fb::CreatefbActorMeta(*m_fbb, owner, controller, prefab, spawnReqId, packedId, bodyType);
	}

	flatbuffers::Offset<fb::fbLifecycleActor> ServerReplicationSystem::BuildLifecycleActor(const PendingLifecycleEvent& event, entt::entity e, NetId netId, uint64 userId)
	{
		if (event.op == fb::fbLifecycleOp_Remove)
			return fb::CreatefbLifecycleActor(*m_fbb, event.op, netId.Raw(), 0, event.reason);

		if (e == entt::null || !m_world.valid(e))
			return 0;

		const auto meta = BuildActorMeta(e, userId);
		if (meta.IsNull())
			return 0;

		return fb::CreatefbLifecycleActor(*m_fbb, event.op, netId.Raw(), meta, event.reason);
	}

	flatbuffers::Offset<fb::fbActorEntity> ServerReplicationSystem::BuildFullActorEntity(entt::entity e, uint64 userId)
	{
		const NetId netId = m_world.get<NetId>(e);
		(void)userId;

		if (const auto* cs = m_world.try_get<CharAuthorityState>(e))
		{
			const auto& state = cs->state;
			auto& sharedState = m_sharedCharacterStates[netId];

			if (sharedState.fullBuiltTick != m_tickCounter)
			{
				++sharedState.fullEpoch;
				sharedState.fullBuiltTick  = m_tickCounter;
				sharedState.hasBaseline    = true;
				sharedState.hasPackedDelta = false;
				sharedState.baseline.pos   = state.pos;
				sharedState.baseline.yaw   = state.facingYaw;
				sharedState.baseline.pitch = state.facingPitch;
			}

			PackedCharacterFull160 packed{};
			if (auto it = m_cachedCharacterFull.find(netId); it != m_cachedCharacterFull.end())
			{
				packed = it->second;
			}
			else
			{
				if (!PackCharacterFull160(state, packed))
					return 0;
				m_cachedCharacterFull.emplace(netId, packed);
			}

			const fb::fbCharacterFull160 charFull(packed.Word(0), packed.Word(1), packed.Word(2), packed.Word(3), packed.Word(4));
			const uint32 baselineEpoch = sharedState.fullEpoch;

			return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), baselineEpoch, nullptr, nullptr, &charFull, nullptr, nullptr);
		}

		if (const auto* rs = m_world.try_get<RigidAuthorityState>(e))
		{
			const auto& state = rs->state;
			auto& sharedState = m_sharedRigidStates[netId];

			if (px::IsLocalDrivenKine(state.kineType))
			{
				if (sharedState.fullBuiltTick != m_tickCounter)
				{
					++sharedState.fullEpoch;
					sharedState.fullBuiltTick  = m_tickCounter;
					sharedState.hasPackedDelta = false;
				}

				uint32 targetNetRaw = NetId::Invalid().Raw();
				if (state.kineState.targetId != px::INVALID_OBJ_ID)
				{
					const entt::entity targetEntity = static_cast<entt::entity>(state.kineState.targetId);
					if (m_world.valid(targetEntity) && m_world.all_of<NetId>(targetEntity))
						targetNetRaw = m_world.get<NetId>(targetEntity).Raw();
				}

				const fb::fbKinematicState kine(state.kineState.startEpoch, state.kineState.phase, state.kineState.t, targetNetRaw, state.kineState.eventMask, static_cast<uint8>(state.kineType));
				return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), sharedState.fullEpoch, nullptr, nullptr, nullptr, nullptr, &kine);
			}

			if (sharedState.fullBuiltTick != m_tickCounter)
			{
				++sharedState.fullEpoch;
				sharedState.fullBuiltTick  = m_tickCounter;
				sharedState.hasBaseline    = true;
				sharedState.hasPackedDelta = false;
				sharedState.baseline.pos   = state.pose.p;
				sharedState.baseline.rot   = state.pose.q;
			}

			PackedRigidFull192 packed{};
			if (auto it = m_cachedRigidFull.find(netId); it != m_cachedRigidFull.end())
			{
				packed = it->second;
			}
			else
			{
				if (!PackRigidFull192(state, packed))
					return 0;
				m_cachedRigidFull.emplace(netId, packed);
			}

			fb::fbTransformFull rigidFull(packed.Word(0), packed.Word(1), packed.Word(2));
			return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), sharedState.fullEpoch, &rigidFull);
		}

		return 0;
	}

	flatbuffers::Offset<fb::fbActorEntity> ServerReplicationSystem::BuildDeltaActorEntity(entt::entity e, uint64 userId)
	{
		const NetId netId = m_world.get<NetId>(e);
		auto& userState = m_userStates[userId];

		if (const auto* cs = m_world.try_get<CharAuthorityState>(e))
		{
			const auto& state = cs->state;
			auto knownEpochIt = userState.knownFullEpoch.find(netId);
			auto& sharedState = m_sharedCharacterStates[netId];
			if (knownEpochIt == userState.knownFullEpoch.end() || knownEpochIt->second != sharedState.fullEpoch || !sharedState.hasBaseline)
				return BuildFullActorEntity(e, userId);

			if (!sharedState.hasPackedDelta || sharedState.lastDeltaSourceState != state)
			{
				if (!PackCharacterDelta128(sharedState.baseline.pos, sharedState.baseline.yaw, sharedState.baseline.pitch, state, sharedState.packedDelta))
					return BuildFullActorEntity(e, userId);
				sharedState.lastDeltaSourceState = state;
				sharedState.hasPackedDelta = true;
			}

			const fb::fbCharacterDelta128 charDelta(sharedState.packedDelta.Word(0), sharedState.packedDelta.Word(1));
			return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), sharedState.fullEpoch, nullptr, nullptr, nullptr, &charDelta);
		}
		
		if (auto* rs = m_world.try_get<RigidAuthorityState>(e))
		{
			auto& state = rs->state;
			auto knownEpochIt = userState.knownFullEpoch.find(netId);
			auto& sharedState = m_sharedRigidStates[netId];
			if (knownEpochIt == userState.knownFullEpoch.end() || knownEpochIt->second != sharedState.fullEpoch)
				return BuildFullActorEntity(e, userId);

			if (px::IsLocalDrivenKine(state.kineType))
			{
				if (state.kineState.startEpoch == 0)
					state.kineState.startEpoch = m_world.ctx().get<TickCounter>().tick;

				uint32 targetNetRaw = NetId::Invalid().Raw();
				if (state.kineState.targetId != px::INVALID_OBJ_ID)
				{
					const entt::entity targetEntity = static_cast<entt::entity>(state.kineState.targetId);
					if (m_world.valid(targetEntity) && m_world.all_of<NetId>(targetEntity))
						targetNetRaw = m_world.get<NetId>(targetEntity).Raw();
				}

				const fb::fbKinematicState kine(state.kineState.startEpoch, state.kineState.phase, state.kineState.t, targetNetRaw, state.kineState.eventMask, E2U(state.kineType));
				return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), sharedState.fullEpoch, nullptr, nullptr, nullptr, nullptr, &kine);
			}

			if (!sharedState.hasBaseline)
				return BuildFullActorEntity(e, userId);

			if (!sharedState.hasPackedDelta || sharedState.lastDeltaSourceState != state)
			{
				if (!PackRigidDelta128(sharedState.baseline.pos, sharedState.baseline.rot, state, sharedState.packedDelta))
					return BuildFullActorEntity(e, userId);
				sharedState.lastDeltaSourceState = state;
				sharedState.hasPackedDelta = true;
			}

			const fb::fbTransformDelta rigidDelta(sharedState.packedDelta.Word(0), sharedState.packedDelta.Word(1));
			return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), sharedState.fullEpoch, nullptr, &rigidDelta);

		}

		return 0;
	}

	void ServerReplicationSystem::QueueLifecycleCreateForUser(uint64 userId, NetId netId)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		auto& pending = m_userStates[userId].pendingLifecycle;
		pending[netId] = PendingLifecycleEvent{ .op = fb::fbLifecycleOp_Create };
		InvalidateUserCaches(userId, netId);
	}

	void ServerReplicationSystem::QueueLifecycleMetaForUser(uint64 userId, NetId netId)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		if (!IsActorKnownToUser(userId, netId))
		{
			QueueLifecycleCreateForUser(userId, netId);
			return;
		}

		auto& pending = m_userStates[userId].pendingLifecycle;
		auto it = pending.find(netId);
		if (it != pending.end())
		{
			if (it->second.op == fb::fbLifecycleOp_Create || it->second.op == fb::fbLifecycleOp_Remove)
				return;
		}

		pending[netId] = PendingLifecycleEvent{ .op = fb::fbLifecycleOp_Meta };
	}

	void ServerReplicationSystem::QueueLifecycleMetaForKnownUser(uint64 userId, NetId netId)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		auto& pending = m_userStates[userId].pendingLifecycle;
		auto it = pending.find(netId);
		if (it != pending.end())
		{
			if (it->second.op == fb::fbLifecycleOp_Create || it->second.op == fb::fbLifecycleOp_Remove)
				return;
		}

		pending[netId] = PendingLifecycleEvent{ .op = fb::fbLifecycleOp_Meta };
	}

	void ServerReplicationSystem::QueueRemovalForUser(uint64 userId, NetId netId, fb::fbRemovalReason reason)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		auto& pending = m_userStates[userId].pendingLifecycle;
		if (!IsActorKnownToUser(userId, netId))
		{
			pending.erase(netId);
			return;
		}

		auto& removal = pending[netId];
		removal.op = fb::fbLifecycleOp_Remove;
		removal.reason = (reason == fb::fbRemovalReason_Destroyed || removal.reason == fb::fbRemovalReason_Destroyed)
			? fb::fbRemovalReason_Destroyed
			: reason;

		InvalidateUserCaches(userId, netId);
	}

	void ServerReplicationSystem::CancelRemovalForUser(uint64 userId, NetId netId)
	{
		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		auto pendingIt = userState->pendingLifecycle.find(netId);
		if (pendingIt == userState->pendingLifecycle.end())
			return;

		if (pendingIt->second.op == fb::fbLifecycleOp_Remove && pendingIt->second.reason != fb::fbRemovalReason_Destroyed)
		{
			userState->pendingLifecycle.erase(pendingIt);
		}
	}

	void ServerReplicationSystem::QueueLifecycleForVisibleActors(uint64 userId, bool forceSyncUser)
	{
		if (userId == 0)
			return;

		const ReplicationUserState* userState = FindUserState(userId);
		const std::unordered_set<NetId>* knownActors = userState ? &userState->knownActors : nullptr;

		auto isKnownActor = [&](NetId id) -> bool
		{
			if (!id.IsValid())
				return false;
			return knownActors && knownActors->contains(id);
		};

		if (!m_aoiSys) return;

		const UserAoiState* state = m_aoiSys->GetState(userId);
		if (!state) return;

		std::unordered_set<uint32> enteredSet;
		enteredSet.reserve(state->entered.size());

		for (const NetId id : state->left)
			QueueRemovalForUser(userId, id, fb::fbRemovalReason_AoiLeft);

		for (const NetId id : state->entered)
		{
		   if (!id.IsValid())
				continue;

			enteredSet.insert(id.Raw());
			CancelRemovalForUser(userId, id);

		 if (isKnownActor(id))
				QueueLifecycleMetaForKnownUser(userId, id);
			else
				QueueLifecycleCreateForUser(userId, id);
		}

		for (const NetId id : state->visible)
		{
			if (!id.IsValid() || enteredSet.contains(id.Raw()))
				continue;

			if (!isKnownActor(id))
			{
				QueueLifecycleCreateForUser(userId, id);
				continue;
			}

			if (forceSyncUser)
			  QueueLifecycleMetaForKnownUser(userId, id);
		}
	}

	//void ServerReplicationSystem::QueueLifecycleForStaticActors(uint64 userId, bool forceSyncUser)
	//{
	//	if (userId == 0)
	//		return;

	//	const ReplicationUserState* userState = FindUserState(userId);
	//	const std::unordered_set<NetId>* knownActors = userState ? &userState->knownActors : nullptr;

	//	auto isKnownActor = [&](NetId id) -> bool
	//	{
	//		if (!id.IsValid())
	//			return false;
	//		return knownActors && knownActors->contains(id);
	//	};

	//	auto staticView = m_world.view<NetId, ReplicationStaticTag>();
	//	for (auto e : staticView)
	//	{
	//		const NetId netId = staticView.get<NetId>(e);
	//		if (!netId.IsValid())
	//			continue;

	//		CancelRemovalForUser(userId, netId);
	//		if (!isKnownActor(netId))
	//		{
	//			QueueLifecycleCreateForUser(userId, netId);
	//			continue;
	//		}

	//		if (forceSyncUser)
	//			QueueLifecycleMetaForKnownUser(userId, netId);
	//	}
	//}

	void ServerReplicationSystem::EmitPendingLifecyclePackets(uint64 userId, uint32 tick)
	{
		auto* userState = FindUserState(userId);
		if (!userState || userState->pendingLifecycle.empty())
			return;

		std::vector<std::pair<NetId, PendingLifecycleEvent>> pending;
		pending.reserve(userState->pendingLifecycle.size());
		for (const auto& [netId, event] : userState->pendingLifecycle)
			pending.emplace_back(netId, event);

		auto estimateEventBytes = [](const PendingLifecycleEvent& event) -> size_t
		{
			switch (event.op)
			{
			case fb::fbLifecycleOp_Create: return 72;
			case fb::fbLifecycleOp_Meta:   return 64;
			case fb::fbLifecycleOp_Remove: return 20;
			default: return 32;
			}
		};

		size_t cursor = 0;
		while (cursor < pending.size())
		{
			m_fbb->Clear();

			std::vector<flatbuffers::Offset<fb::fbLifecycleActor>> actorOffs;
			std::vector<std::pair<NetId, PendingLifecycleEvent>>   sentEvents;
			actorOffs.reserve(kLifecycleBatch);
			sentEvents.reserve(kLifecycleBatch);

			size_t usedPayloadBudget = 0;
			for (; cursor < pending.size(); ++cursor)
			{
				if (actorOffs.size() >= kLifecycleBatch)
					break;

				const auto& [netId, event] = pending[cursor];
				const size_t est = estimateEventBytes(event);
				if (est > kPacketPayloadBudget)
					continue;

				if ((usedPayloadBudget + est) > kPacketPayloadBudget && !actorOffs.empty())
				{
					JAMNET_LOG_WARN("[EmitPendingLifecyclePackets] user id= {}, pending packets size is over budget");
					break;
				}

				entt::entity e = entt::null;
				if (event.op != fb::fbLifecycleOp_Remove)
				{
					e = m_netWorld->GetEntity(netId);
				}

				const auto off = BuildLifecycleActor(event, e, netId, userId);
				if (off.IsNull())
					continue;

				actorOffs.push_back(off);
				sentEvents.emplace_back(netId, event);
				usedPayloadBudget += est;
			}

			if (actorOffs.empty())
				break;

			const auto vec = m_fbb->CreateVector(actorOffs);
			const auto batch = fb::CreatefbLifecycleBatch(*m_fbb, m_netWorld->GetWorldId(), tick, vec);
			m_fbb->Finish(batch, fb::fbLifecycleBatchIdentifier());

			auto pkt = PacketBuilder::CreateCustomPacket(CustomPacketId::LIFECYCLE, PacketFlags::NONE, eChannel::RELIABLE_ORDERED, m_fbb->GetBufferPointer(), m_fbb->GetSize());
			if (!pkt.IsValid())
				continue;


			m_netWorld->SendTo(pkt, userId);

			CommitPendingLifecycleBatch(userId, sentEvents);
		}
	}

	void ServerReplicationSystem::CommitPendingLifecycleBatch(uint64 userId, const std::vector<std::pair<NetId, PendingLifecycleEvent>>& sentEvents)
	{
		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		auto* nw = m_netWorld;

		for (const auto& [netId, event] : sentEvents)
		{
			auto pendingIt = userState->pendingLifecycle.find(netId);
			if (pendingIt == userState->pendingLifecycle.end())
				continue;

			if (event.op == fb::fbLifecycleOp_Create)
			{
				MarkActorKnownToUser(userId, netId);
				userState->forceFullStateBudget[netId] = kCreateFullStateBudget;

				const entt::entity e = nw ? nw->GetEntity(netId) : entt::null;
				if (e != entt::null && m_world.valid(e) && m_world.all_of<OwnershipTag>(e))
				{
					if (m_world.get<OwnershipTag>(e).userId == userId && m_world.all_of<NetSpawnRequestId>(e))
						m_world.remove<NetSpawnRequestId>(e);
				}
			}
			else if (event.op == fb::fbLifecycleOp_Remove && event.reason == fb::fbRemovalReason_Destroyed)
			{
				ForgetActorForUser(userId, netId);
			}
			else if (event.op == fb::fbLifecycleOp_Remove && event.reason == fb::fbRemovalReason_AoiLeft)
			{
				ForgetActorForUser(userId, netId);
			}

			userState->pendingLifecycle.erase(pendingIt);
		}
	}

	void ServerReplicationSystem::InvalidateUserCaches(uint64 userId, NetId netId)
	{
		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		userState->knownFullEpoch.erase(netId);
	}

	void ServerReplicationSystem::InvalidateAllUserCaches(NetId netId)
	{
		if (auto it = m_knownUsersByActor.find(netId); it != m_knownUsersByActor.end())
		{
			for (const KnownUserSlot& slot : it->second)
			{
				if (!slot.alive)
					continue;

				const uint64 userId = slot.userId;
				if (auto* userState = FindUserState(userId))
					userState->knownFullEpoch.erase(netId);
			}
		}
	}

	bool ServerReplicationSystem::IsActorKnownToUser(uint64 userId, NetId netId) const
	{
		if (userId == 0 || !netId.IsValid())
			return false;

		if (const auto* userState = FindUserState(userId))
			return userState->knownActors.contains(netId);
		return false;
	}

	void ServerReplicationSystem::MarkActorKnownToUser(uint64 userId, NetId netId)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		m_userStates[userId].knownActors.insert(netId);
		AddKnownUserToActor(netId, userId);
	}

	void ServerReplicationSystem::ForgetActorForUser(uint64 userId, NetId netId)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		userState->knownActors.erase(netId);
		userState->knownFullEpoch.erase(netId);
		userState->forceFullStateBudget.erase(netId);
		RemoveKnownUserFromActor(netId, userId);
	}

	void ServerReplicationSystem::ForgetActorForAllUsers(NetId netId)
	{
		if (!netId.IsValid())
			return;

		for (auto& userState : m_userStates | std::views::values)
		{
			userState.knownActors.erase(netId);
			userState.knownFullEpoch.erase(netId);
			userState.forceFullStateBudget.erase(netId);
		}
		m_knownUsersByActor.erase(netId);
	}

	bool ServerReplicationSystem::ShouldForceFullState(uint64 userId, NetId netId) const
	{
		if (userId == 0 || !netId.IsValid())
			return false;

		if (const auto* userState = FindUserState(userId))
		{
			if (auto it = userState->forceFullStateBudget.find(netId); it != userState->forceFullStateBudget.end())
				return it->second > 0;
		}
		return false;
	}

	void ServerReplicationSystem::MarkFullStateSent(uint64 userId, NetId netId)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		uint32 actorEpoch = 0;
		if (const auto it = m_sharedCharacterStates.find(netId); it != m_sharedCharacterStates.end())
			actorEpoch = it->second.fullEpoch;
		else if (const auto it = m_sharedRigidStates.find(netId); it != m_sharedRigidStates.end())
			actorEpoch = it->second.fullEpoch;

		if (actorEpoch != 0)
			userState->knownFullEpoch[netId] = actorEpoch;

		auto jt = userState->forceFullStateBudget.find(netId);
		if (jt == userState->forceFullStateBudget.end())
			return;

		if (jt->second > 0)
			--jt->second;

		if (jt->second == 0)
			userState->forceFullStateBudget.erase(jt);
	}
}
