#include "pch.h"
#include "jamnet/runtime/world/simulation/server/ServerReplicationSystem.h"
#include "jamnet/runtime/world/simulation/server/WorldMetrics.h"

#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"

#include "jamnet/runtime/world/simulation/server/ServerWorld.h"
#include "jamnet/runtime/world/simulation/server/ServerInputSystem.h"
#include "jamnet/runtime/world/simulation/server/ServerPhysicsSystem.h"
#include "jamnet/runtime/world/simulation/server/ServerAoiSystem.h"

#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/protocol/codec/ReplicationCodec.h"


namespace jam::net
{
	namespace
	{
		constexpr size_t kLifecycleBatch = 96;
		constexpr size_t kSnapshotBatch = 128;
		constexpr size_t kPacketPayloadBudget = JAMNET_MTU - PacketHeader::HALF_SIZE - 24;

		struct PlannedSnapshotCandidate
		{
			entt::entity e = entt::null;
			ActorId actorId = ActorId::Invalid();
			bool useFull = false;
		};

		struct PlannedSnapshotChunk
		{
			std::vector<PlannedSnapshotCandidate> candidates;
		};
	}

	ServerReplicationSystem::ServerReplicationSystem(entt::registry& world, WorldMetrics& metrics)
		: m_world(world), m_metrics(&metrics)
	{
		m_fbb.reset(new flatbuffers::FlatBufferBuilder(JAMNET_MTU));
	}

	void ServerReplicationSystem::Init()
	{
		m_tickCounter	 = 0;
		m_netWorld = nullptr;
		if (auto* nw = m_world.ctx().find<ServerWorld*>())
			m_netWorld = (nw && *nw) ? *nw : nullptr;
		m_inputSys  = m_world.ctx().find<ServerInputSystem>();
		m_aoiSys	= m_world.ctx().find<ServerAoiSystem>();
		m_physSys	= m_world.ctx().find<ServerPhysicsSystem>();

		m_userStates.clear();
		m_knownUsersByActor.clear();
		m_sharedRigidStates.clear();
		m_sharedCharacterStates.clear();
		m_actorFrameCache.clear();
		m_actorFrameActorIds.clear();
		m_knownUsersScratch.clear();
		m_sentThisTickScratch.clear();
		m_enteredScratch.clear();
		for (auto& bucket : m_candidateBucketsScratch)
			bucket.clear();
		m_orderedCandidatesScratch.clear();
		m_actorOffsScratch.clear();
		m_fullActorIdsScratch.clear();
		m_dirtyActorFrameScratch.clear();
		m_dirtyActorFrameDedup.clear();
		m_prevActiveActors.clear();
		m_currentActiveActorsScratch.clear();

		auto frameSeedView = m_world.view<ActorId, ActorBodyType>(entt::exclude<ReplicationDisabledTag>);
		for (auto e : frameSeedView)
			MarkActorFrameDirty(e);

	}

	void ServerReplicationSystem::Tick()
	{
		++m_tickCounter;
		CaptureSnapshot();
	}

	void ServerReplicationSystem::CaptureSnapshot()
	{
		if (!m_netWorld || !m_aoiSys) return;

		RefreshActorFrameCache();

		const uint32 tick = m_world.ctx().get<TickCounter>().tick;

		if (m_userStates.empty()) return;

		auto* inputSys = m_inputSys;

		for (auto& [user, userState] : m_userStates)
		{
			if (userState.phase == eReplicationPhase::AwaitingPlayer
				|| userState.phase == eReplicationPhase::Suspended)
				continue;
			if (userState.phase == eReplicationPhase::NeedsResync)
			{
				userState.baselineDelivery.clear();
				userState.phase = eReplicationPhase::InitialSync;
			}
			const uint32 ack		= inputSys ? inputSys->LastAppliedSeq(user) : 0;
			const uint32 inputEpoch = inputSys ? inputSys->LastAppliedControlRevision(user) : 0;

			m_sentThisTickScratch.clear();
			m_sentThisTickScratch.reserve(256);

			m_enteredScratch.clear();

			if (const UserAoiState* st = m_aoiSys->GetState(user))
			{
				m_enteredScratch.reserve(st->entered.size());
				for (const ActorId id : st->entered)
					m_enteredScratch.insert(id.Value());
			}

			QueueLifecycleForVisibleActors(user);
			EmitPendingLifecyclePackets(user, tick);

			for (auto& bucket : m_candidateBucketsScratch)
				bucket.clear();

			auto addCandidate = [&](entt::entity e, ActorId actorId)
			{
				const ActorFrameCache* actorFrame = FindActorFrameCache(actorId);
				if (!actorFrame || actorFrame->e == entt::null || actorFrame->isStatic || !actorFrame->canReplicate)
					return;

				const uint32 actorEpoch		 = GetActorFullEpoch(actorFrame->e, actorId);
				const bool baselineInvalid = !CanSendDelta(user, actorId, actorEpoch);

				const bool enteredNow = m_enteredScratch.contains(actorId.Value());

				Candidate c{ .e = actorFrame->e, .actorId = actorId };

				if (baselineInvalid || enteredNow)
				{
					if (!ShouldSendFull(user, actorId, actorEpoch))
						return;
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
				for (const AoiVisibleActorSlot& slot : *visibleActors)
				{
					if (!slot.alive || slot.actor == entt::null || !m_world.valid(slot.actor) || !m_world.all_of<ActorId>(slot.actor))
						continue;
					addCandidate(slot.actor, m_world.get<ActorId>(slot.actor));
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
				if (const ActorFrameCache* actorFrame = FindActorFrameCache(c.actorId))
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
						JAMNET_LOG_WARN("[Snapshot] single actor too large. user={}, actorId={}", user, c.actorId.Value());
						continue;
					}

					if ((usedPayloadBudget + est) > kPacketPayloadBudget)
					{
						if (!chunkPlan.candidates.empty())
							break;
						continue;
					}

					if (m_sentThisTickScratch.contains(c.actorId))
						continue;

					m_sentThisTickScratch.insert(c.actorId);
					chunkPlan.candidates.push_back(PlannedSnapshotCandidate{
						.e = c.e,
						.actorId = c.actorId,
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
				m_fullActorIdsScratch.clear();
				m_fullActorIdsScratch.reserve(plannedChunks[chunkIndex].candidates.size());

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
						m_fullActorIdsScratch.push_back(c.actorId);
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
				m_metrics->RecordSnapshotPacket(pkt->Size(), m_actorOffsScratch.size(), m_fullActorIdsScratch.size());
				for (const ActorId actorId : m_fullActorIdsScratch)
					MarkFullStateSent(user, actorId);
			}

			TryCompleteInitialSync(user);
		}

		uint64 pendingLifecycleTotal = 0;
		for (const auto& state : m_userStates | std::views::values)
			pendingLifecycleTotal += state.pendingLifecycle.size();
		m_metrics->SetLifecyclePending(pendingLifecycleTotal);
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


	uint32 ServerReplicationSystem::GetActorFullEpoch(entt::entity e, ActorId actorId)
	{
		if (e == entt::null || !m_world.valid(e))
			return 0;

		const auto* body = m_world.try_get<ActorBodyType>(e);
		if (!body)
			return 0;

		if (body->body == px::eBodyType::Character)
		{
			if (const auto it = m_sharedCharacterStates.find(actorId); it != m_sharedCharacterStates.end())
				return it->second.fullEpoch;
			return 0;
		}

		if (body->body == px::eBodyType::Rigid)
		{
			if (const auto it = m_sharedRigidStates.find(actorId); it != m_sharedRigidStates.end())
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

	const ServerReplicationSystem::ActorFrameCache* ServerReplicationSystem::FindActorFrameCache(ActorId actorId) const
	{
		if (const auto it = m_actorFrameCache.find(actorId); it != m_actorFrameCache.end())
			return &it->second;
		return nullptr;
	}

	void ServerReplicationSystem::AddKnownUserToActor(ActorId actorId, uint64 userId)
	{
		auto& slots = m_knownUsersByActor[actorId];
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

	void ServerReplicationSystem::RemoveKnownUserFromActor(ActorId actorId, uint64 userId)
	{
		auto it = m_knownUsersByActor.find(actorId);
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

		CompactKnownUsersIfNeeded(actorId);
	}

	void ServerReplicationSystem::CompactKnownUsersIfNeeded(ActorId actorId)
	{
		auto it = m_knownUsersByActor.find(actorId);
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
		const auto prevActorIdIt = m_actorFrameActorIds.find(e);
		if (e == entt::null || !m_world.valid(e) || !m_world.all_of<ActorId, ActorBodyType>(e)
			|| m_world.all_of<ReplicationDisabledTag>(e))
		{
			if (prevActorIdIt != m_actorFrameActorIds.end())
			{
				m_actorFrameCache.erase(prevActorIdIt->second);
				m_actorFrameActorIds.erase(prevActorIdIt);
			}
			return;
		}

		const ActorId actorId = m_world.get<ActorId>(e);
		const px::eBodyType bodyType = m_world.get<ActorBodyType>(e).body;
		if (!actorId.IsValid())
		{
			if (prevActorIdIt != m_actorFrameActorIds.end())
			{
				m_actorFrameCache.erase(prevActorIdIt->second);
				m_actorFrameActorIds.erase(prevActorIdIt);
			}
			return;
		}

		if (prevActorIdIt != m_actorFrameActorIds.end() && prevActorIdIt->second != actorId)
			m_actorFrameCache.erase(prevActorIdIt->second);

		ActorFrameCache frame{};
		frame.e = e;
		frame.actorId = actorId;
		frame.fullEpoch = GetActorFullEpoch(e, actorId);
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

		m_actorFrameCache[actorId] = frame;
		m_actorFrameActorIds[e] = actorId;
	}

	void ServerReplicationSystem::MarkActorDirty(entt::entity e, bool forceMeta)
	{
		if (!m_world.valid(e) || !m_world.all_of<ActorId>(e))
			return;

		const ActorId actorId = m_world.get<ActorId>(e);
		if (!actorId.IsValid())
			return;

		MarkActorFrameDirty(e);

		if (!forceMeta)
			return;

		auto* nw = m_netWorld;
		if (!nw)
			return;

		if (auto it = m_knownUsersByActor.find(actorId); it != m_knownUsersByActor.end())
		{
			m_knownUsersScratch.clear();
			m_knownUsersScratch.reserve(it->second.size());
			for (const KnownUserSlot& slot : it->second)
			{
				if (slot.alive)
					m_knownUsersScratch.push_back(slot.userId);
			}

			for (const uint64 userId : m_knownUsersScratch)
				QueueLifecycleMetaForUser(userId, actorId);
		}
	}

	bool ServerReplicationSystem::AttachUser(uint64 userId)
	{
		if (userId == 0)
			return false;

		auto [it, inserted] = m_userStates.try_emplace(userId, ReplicationUserState
			{
				.phase = eReplicationPhase::AwaitingPlayer,
			});
		if (!inserted)
			return it->second.phase == eReplicationPhase::AwaitingPlayer;

		return true;
	}

	bool ServerReplicationSystem::BeginInitialSync(uint64 userId)
	{
		auto* userState = FindUserState(userId);
		if (!userState || userState->phase != eReplicationPhase::AwaitingPlayer)
			return false;

		userState->phase = eReplicationPhase::InitialSync;
		return true;
	}

	bool ServerReplicationSystem::SuspendUser(uint64 userId)
	{
		auto* userState = FindUserState(userId);
		if (!userState)
			return false;

		userState->phase = eReplicationPhase::Suspended;
		return true;
	}

	bool ServerReplicationSystem::ResumeUserWithFullSync(uint64 userId)
	{
		auto* userState = FindUserState(userId);
		if (!userState || userState->phase != eReplicationPhase::Suspended)
			return false;

		for (const ActorId actorId : userState->knownActors)
			RemoveKnownUserFromActor(actorId, userId);
		userState->knownActors.clear();
		userState->baselineDelivery.clear();
		userState->pendingLifecycle.clear();
		userState->phase = eReplicationPhase::InitialSync;
		return true;
	}

	bool ServerReplicationSystem::IsAwaitingPlayer(uint64 userId) const
	{
		const auto* userState = FindUserState(userId);
		return userState && userState->phase == eReplicationPhase::AwaitingPlayer;
	}

	void ServerReplicationSystem::HandleBaselineFeedback(uint64 userId, const fb::fbBaselineAckBatch& batch)
	{
		auto* userState = FindUserState(userId);
		if (!userState || userState->phase == eReplicationPhase::AwaitingPlayer
			|| userState->phase == eReplicationPhase::Suspended || !batch.entries())
			return;

		for (const auto* entry : *batch.entries())
		{
			if (!entry)
				continue;
			const ActorId actorId = ActorId(entry->actor_id());
			if (!actorId.IsValid() || !userState->knownActors.contains(actorId))
				continue;

			const entt::entity entity = m_netWorld ? m_netWorld->ResolveActor(actorId) : entt::null;
			if (entity == entt::null || !m_world.valid(entity))
				continue;
			const uint32 currentEpoch = GetActorFullEpoch(entity, actorId);
			auto& delivery = userState->baselineDelivery[actorId];

			if (entry->request_full())
			{
				m_metrics->RecordBaselineFullRequest();
				delivery.sentBaselineRev = 0;
				delivery.ackedBaselineRev = 0;
				delivery.resendCount = 0;
				continue;
			}

			if (entry->baseline_rev() != currentEpoch || entry->baseline_rev() != delivery.sentBaselineRev)
			{
				delivery.sentBaselineRev = 0;
				delivery.ackedBaselineRev = 0;
				delivery.resendCount = 0;
				continue;
			}

			delivery.ackedBaselineRev = entry->baseline_rev();
			delivery.resendCount = 0;
		}

		TryCompleteInitialSync(userId);
	}

	void ServerReplicationSystem::OnUserLeave(uint64 userId)
	{
		if (userId == 0)
			return;

		if (auto* userState = FindUserState(userId))
		{
			for (const ActorId actorId : userState->knownActors)
				RemoveKnownUserFromActor(actorId, userId);
		}

		m_userStates.erase(userId);
	}

	void ServerReplicationSystem::OnActorDestroyed(entt::entity e)
	{
		if (!m_world.valid(e) || !m_world.all_of<ActorId>(e))
			return;

		const ActorId actorId = m_world.get<ActorId>(e);
		if (!actorId.IsValid())
			return;

		InvalidateAllUserCaches(actorId);

		if (auto it = m_knownUsersByActor.find(actorId); it != m_knownUsersByActor.end())
		{
			m_knownUsersScratch.clear();
			m_knownUsersScratch.reserve(it->second.size());
			for (const KnownUserSlot& slot : it->second)
			{
				if (slot.alive)
					m_knownUsersScratch.push_back(slot.userId);
			}

			for (const uint64 userId : m_knownUsersScratch)
				QueueRemovalForUser(userId, actorId, fb::fbRemovalReason_Destroyed);
		}

		ForgetActorForAllUsers(actorId);
		m_actorFrameCache.erase(actorId);
		m_actorFrameActorIds.erase(e);
		m_prevActiveActors.erase(e);
		m_currentActiveActorsScratch.erase(e);
		m_dirtyActorFrameDedup.erase(e);
		m_sharedRigidStates.erase(actorId);
		m_sharedCharacterStates.erase(actorId);
	}

	flatbuffers::Offset<fb::fbActorMeta> ServerReplicationSystem::BuildActorMeta(entt::entity e, uint64 userId)
	{
		uint64 owner				= 0;
		uint64 controller			= 0;
		uint64 actorArchetypeKey	= 0;
		uint32 clientRequestId		= 0;
		const auto* body = m_world.try_get<ActorBodyType>(e);
		if (!body || body->body == px::eBodyType::None)
			return 0;

		const uint8 bodyType = static_cast<uint8>(body->body);

		if (auto* o   = m_world.try_get<OwnershipTag>(e))			owner			 = o->userId;
		if (auto* c   = m_world.try_get<ControlTag>(e))				controller		 = c->userId;
		if (auto* a   = m_world.try_get<ActorArchetypeRef>(e))	actorArchetypeKey = a->key.v;

		if (userId != 0 && userId == owner)
		{
			if (auto* s = m_world.try_get<ClientRequestCorrelation>(e))
				clientRequestId = s->requestId;
		}

		return fb::CreatefbActorMeta(*m_fbb, owner, controller, actorArchetypeKey, clientRequestId, bodyType);
	}

	flatbuffers::Offset<fb::fbLifecycleActor> ServerReplicationSystem::BuildLifecycleActor(const PendingLifecycleEvent& event, entt::entity e, ActorId actorId, uint64 userId)
	{
		if (event.op == fb::fbLifecycleOp_Remove)
			return fb::CreatefbLifecycleActor(*m_fbb, event.op, actorId.Value(), 0, event.reason);

		if (e == entt::null || !m_world.valid(e))
			return 0;

		const auto meta = BuildActorMeta(e, userId);
		if (meta.IsNull())
			return 0;

		return fb::CreatefbLifecycleActor(*m_fbb, event.op, actorId.Value(), meta, event.reason);
	}

	flatbuffers::Offset<fb::fbActorEntity> ServerReplicationSystem::BuildFullActorEntity(entt::entity e, uint64 userId)
	{
		const ActorId actorId = m_world.get<ActorId>(e);
		(void)userId;

		if (const auto* cs = m_world.try_get<CharAuthorityState>(e))
		{
			const auto& state = cs->state;
			auto& sharedState = m_sharedCharacterStates[actorId];

			if (!sharedState.hasBaseline)
			{
				++sharedState.fullEpoch;
				sharedState.fullBuiltTick  = m_tickCounter;
				sharedState.hasBaseline    = true;
				sharedState.hasPackedDelta = false;
				sharedState.baseline.pos   = state.pos;
				sharedState.baseline.bodyYaw = state.bodyYaw;
				sharedState.baseline.viewYaw = state.viewYaw;
				sharedState.baseline.pitch = state.viewPitch;
				if (!PackCharacterFull160(state, sharedState.packedFull))
					return 0;
			}

			const auto& packed = sharedState.packedFull;
			const fb::fbCharacterFull160 charFull(packed.Word(0), packed.Word(1), packed.Word(2), packed.Word(3), packed.Word(4));
			const uint32 baselineEpoch = sharedState.fullEpoch;

			return fb::CreatefbActorEntity(*m_fbb, actorId.Value(), baselineEpoch, nullptr, nullptr, &charFull, nullptr, nullptr);
		}

		if (const auto* rs = m_world.try_get<RigidAuthorityState>(e))
		{
			const auto& state = rs->state;
			auto& sharedState = m_sharedRigidStates[actorId];

			if (px::IsLocalDrivenKine(state.kineType))
			{
				if (sharedState.fullBuiltTick != m_tickCounter)
				{
					++sharedState.fullEpoch;
					sharedState.fullBuiltTick  = m_tickCounter;
					sharedState.hasPackedDelta = false;
				}

				uint32 targetActorRaw = ActorId::Invalid().Value();
				if (state.kineState.targetActorId != px::INVALID_ACTOR_ID)
				{
					const entt::entity targetEntity = m_netWorld ? m_netWorld->ResolveActor(ActorId(state.kineState.targetActorId)) : entt::null;
					if (m_world.valid(targetEntity) && m_world.all_of<ActorId>(targetEntity))
						targetActorRaw = m_world.get<ActorId>(targetEntity).Value();
				}

				const fb::fbKinematicState kine(state.kineState.startEpoch, state.kineState.phase, state.kineState.t, targetActorRaw, state.kineState.eventMask, static_cast<uint8>(state.kineType));
				return fb::CreatefbActorEntity(*m_fbb, actorId.Value(), sharedState.fullEpoch, nullptr, nullptr, nullptr, nullptr, &kine);
			}

			if (!sharedState.hasBaseline)
			{
				++sharedState.fullEpoch;
				sharedState.fullBuiltTick  = m_tickCounter;
				sharedState.hasBaseline    = true;
				sharedState.hasPackedDelta = false;
				sharedState.baseline.pos   = state.pose.p;
				sharedState.baseline.rot   = state.pose.q;
				if (!PackRigidFull192(state, sharedState.packedFull))
					return 0;
			}

			const auto& packed = sharedState.packedFull;
			fb::fbTransformFull rigidFull(packed.Word(0), packed.Word(1), packed.Word(2));
			return fb::CreatefbActorEntity(*m_fbb, actorId.Value(), sharedState.fullEpoch, &rigidFull);
		}

		return 0;
	}

	flatbuffers::Offset<fb::fbActorEntity> ServerReplicationSystem::BuildDeltaActorEntity(entt::entity e, uint64 userId)
	{
		const ActorId actorId = m_world.get<ActorId>(e);
		auto* userStatePtr = FindUserState(userId);
		if (!userStatePtr || userStatePtr->phase != eReplicationPhase::Streaming)
			return BuildFullActorEntity(e, userId);
		auto& userState = *userStatePtr;

		if (const auto* cs = m_world.try_get<CharAuthorityState>(e))
		{
			const auto& state = cs->state;
			auto& sharedState = m_sharedCharacterStates[actorId];
			if (!CanSendDelta(userId, actorId, sharedState.fullEpoch) || !sharedState.hasBaseline)
				return BuildFullActorEntity(e, userId);

			if (!sharedState.hasPackedDelta || sharedState.lastDeltaSourceState != state)
			{
				if (!PackCharacterDelta128(sharedState.baseline.pos, sharedState.baseline.bodyYaw, sharedState.baseline.viewYaw, sharedState.baseline.pitch, state, sharedState.packedDelta))
				{
					sharedState.hasBaseline = false;
					return BuildFullActorEntity(e, userId);
				}
				sharedState.lastDeltaSourceState = state;
				sharedState.hasPackedDelta = true;
			}

			const fb::fbCharacterDelta128 charDelta(sharedState.packedDelta.Word(0), sharedState.packedDelta.Word(1));
			return fb::CreatefbActorEntity(*m_fbb, actorId.Value(), sharedState.fullEpoch, nullptr, nullptr, nullptr, &charDelta);
		}
		
		if (auto* rs = m_world.try_get<RigidAuthorityState>(e))
		{
			auto& state = rs->state;
			auto& sharedState = m_sharedRigidStates[actorId];
			if (!CanSendDelta(userId, actorId, sharedState.fullEpoch))
				return BuildFullActorEntity(e, userId);

			if (px::IsLocalDrivenKine(state.kineType))
			{
				if (state.kineState.startEpoch == 0)
					state.kineState.startEpoch = m_world.ctx().get<TickCounter>().tick;

				uint32 targetActorRaw = ActorId::Invalid().Value();
				if (state.kineState.targetActorId != px::INVALID_ACTOR_ID)
				{
					const entt::entity targetEntity = m_netWorld ? m_netWorld->ResolveActor(ActorId(state.kineState.targetActorId)) : entt::null;
					if (m_world.valid(targetEntity) && m_world.all_of<ActorId>(targetEntity))
						targetActorRaw = m_world.get<ActorId>(targetEntity).Value();
				}

				const fb::fbKinematicState kine(state.kineState.startEpoch, state.kineState.phase, state.kineState.t, targetActorRaw, state.kineState.eventMask, E2U(state.kineType));
				return fb::CreatefbActorEntity(*m_fbb, actorId.Value(), sharedState.fullEpoch, nullptr, nullptr, nullptr, nullptr, &kine);
			}

			if (!sharedState.hasBaseline)
				return BuildFullActorEntity(e, userId);

			if (!sharedState.hasPackedDelta || sharedState.lastDeltaSourceState != state)
			{
				if (!PackRigidDelta128(sharedState.baseline.pos, sharedState.baseline.rot, state, sharedState.packedDelta))
				{
					sharedState.hasBaseline = false;
					return BuildFullActorEntity(e, userId);
				}
				sharedState.lastDeltaSourceState = state;
				sharedState.hasPackedDelta = true;
			}

			const fb::fbTransformDelta rigidDelta(sharedState.packedDelta.Word(0), sharedState.packedDelta.Word(1));
			return fb::CreatefbActorEntity(*m_fbb, actorId.Value(), sharedState.fullEpoch, nullptr, &rigidDelta);

		}

		return 0;
	}

	void ServerReplicationSystem::QueueLifecycleCreateForUser(uint64 userId, ActorId actorId)
	{
		if (userId == 0 || !actorId.IsValid())
			return;

		auto* userState = FindUserState(userId);
		if (!userState)
			return;
		auto& pending = userState->pendingLifecycle;
		pending[actorId] = PendingLifecycleEvent{ .op = fb::fbLifecycleOp_Create };
		InvalidateUserCaches(userId, actorId);
	}

	void ServerReplicationSystem::QueueLifecycleMetaForUser(uint64 userId, ActorId actorId)
	{
		if (userId == 0 || !actorId.IsValid())
			return;

		if (!IsActorKnownToUser(userId, actorId))
		{
			QueueLifecycleCreateForUser(userId, actorId);
			return;
		}

		auto* userState = FindUserState(userId);
		if (!userState)
			return;
		auto& pending = userState->pendingLifecycle;
		auto it = pending.find(actorId);
		if (it != pending.end())
		{
			if (it->second.op == fb::fbLifecycleOp_Create || it->second.op == fb::fbLifecycleOp_Remove)
				return;
		}

		pending[actorId] = PendingLifecycleEvent{ .op = fb::fbLifecycleOp_Meta };
	}

	void ServerReplicationSystem::QueueLifecycleMetaForKnownUser(uint64 userId, ActorId actorId)
	{
		if (userId == 0 || !actorId.IsValid())
			return;

		auto* userState = FindUserState(userId);
		if (!userState)
			return;
		auto& pending = userState->pendingLifecycle;
		auto it = pending.find(actorId);
		if (it != pending.end())
		{
			if (it->second.op == fb::fbLifecycleOp_Create || it->second.op == fb::fbLifecycleOp_Remove)
				return;
		}

		pending[actorId] = PendingLifecycleEvent{ .op = fb::fbLifecycleOp_Meta };
	}

	void ServerReplicationSystem::QueueRemovalForUser(uint64 userId, ActorId actorId, fb::fbRemovalReason reason)
	{
		if (userId == 0 || !actorId.IsValid())
			return;

		auto* userState = FindUserState(userId);
		if (!userState)
			return;
		auto& pending = userState->pendingLifecycle;
		if (!IsActorKnownToUser(userId, actorId))
		{
			pending.erase(actorId);
			return;
		}

		auto& removal = pending[actorId];
		removal.op = fb::fbLifecycleOp_Remove;
		removal.reason = (reason == fb::fbRemovalReason_Destroyed || removal.reason == fb::fbRemovalReason_Destroyed)
			? fb::fbRemovalReason_Destroyed
			: reason;

		InvalidateUserCaches(userId, actorId);
	}

	void ServerReplicationSystem::CancelRemovalForUser(uint64 userId, ActorId actorId)
	{
		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		auto pendingIt = userState->pendingLifecycle.find(actorId);
		if (pendingIt == userState->pendingLifecycle.end())
			return;

		if (pendingIt->second.op == fb::fbLifecycleOp_Remove && pendingIt->second.reason != fb::fbRemovalReason_Destroyed)
		{
			userState->pendingLifecycle.erase(pendingIt);
		}
	}

	void ServerReplicationSystem::QueueLifecycleForVisibleActors(uint64 userId)
	{
		if (userId == 0)
			return;

		const ReplicationUserState* userState = FindUserState(userId);
		const std::unordered_set<ActorId>* knownActors = userState ? &userState->knownActors : nullptr;

		auto isKnownActor = [&](ActorId id) -> bool
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

		for (const ActorId id : state->left)
			QueueRemovalForUser(userId, id, fb::fbRemovalReason_AoiLeft);

		for (const ActorId id : state->entered)
		{
			if (!id.IsValid())
				continue;

			enteredSet.insert(id.Value());
			CancelRemovalForUser(userId, id);

			if (isKnownActor(id))
				QueueLifecycleMetaForKnownUser(userId, id);
			else
				QueueLifecycleCreateForUser(userId, id);
		}

		for (const ActorId id : state->visible)
		{
			if (!id.IsValid() || enteredSet.contains(id.Value()))
				continue;

			if (!isKnownActor(id))
			{
				QueueLifecycleCreateForUser(userId, id);
				continue;
			}
		}
	}


	void ServerReplicationSystem::EmitPendingLifecyclePackets(uint64 userId, uint32 tick)
	{
		auto* userState = FindUserState(userId);
		if (!userState || userState->pendingLifecycle.empty())
			return;

		std::vector<std::pair<ActorId, PendingLifecycleEvent>> pending;
		pending.reserve(userState->pendingLifecycle.size());
		for (const auto& [actorId, event] : userState->pendingLifecycle)
			pending.emplace_back(actorId, event);

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
			std::vector<std::pair<ActorId, PendingLifecycleEvent>>   sentEvents;
			actorOffs.reserve(kLifecycleBatch);
			sentEvents.reserve(kLifecycleBatch);

			size_t usedPayloadBudget = 0;
			for (; cursor < pending.size(); ++cursor)
			{
				if (actorOffs.size() >= kLifecycleBatch)
					break;

				const auto& [actorId, event] = pending[cursor];
				const size_t est = estimateEventBytes(event);
				if (est > kPacketPayloadBudget)
					continue;

				if ((usedPayloadBudget + est) > kPacketPayloadBudget && !actorOffs.empty())
				{
					m_metrics->RecordLifecyclePacketSplit();
					break;
				}

				entt::entity e = entt::null;
				if (event.op != fb::fbLifecycleOp_Remove)
				{
					e = m_netWorld->ResolveActor(actorId);
				}

				const auto off = BuildLifecycleActor(event, e, actorId, userId);
				if (off.IsNull())
					continue;

				actorOffs.push_back(off);
				sentEvents.emplace_back(actorId, event);
				usedPayloadBudget += est;
			}

			if (actorOffs.empty())
				break;

			const auto vec = m_fbb->CreateVector(actorOffs);
			const auto batch = fb::CreatefbLifecycleBatch(*m_fbb, m_netWorld->GetWorldId(), tick, vec);
			m_fbb->Finish(batch, fb::fbLifecycleBatchIdentifier());

			auto pkt = PacketBuilder::CreateCustomPacket(CustomPacketId::LIFECYCLE, PacketFlags::NONE, eChannel::RELIABLE_ORDERED, m_fbb->GetBufferPointer(), m_fbb->GetSize());
			if (!pkt.IsValid())
			{
				JAMNET_LOG_WARN("[LifecycleTx] packet build failed. userId={}, worldId={}, tick={}, actors={}, payloadBytes={}",
					userId, m_netWorld->GetWorldId(), tick, actorOffs.size(), m_fbb->GetSize());
				continue;
			}

			m_netWorld->SendTo(pkt, userId);
			m_metrics->RecordLifecyclePacket(pkt->Size(), sentEvents.size());

			CommitPendingLifecycleBatch(userId, sentEvents);
		}
	}

	void ServerReplicationSystem::CommitPendingLifecycleBatch(uint64 userId, const std::vector<std::pair<ActorId, PendingLifecycleEvent>>& sentEvents)
	{
		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		auto* nw = m_netWorld;

		for (const auto& [actorId, event] : sentEvents)
		{
			auto pendingIt = userState->pendingLifecycle.find(actorId);
			if (pendingIt == userState->pendingLifecycle.end())
				continue;

			if (event.op == fb::fbLifecycleOp_Create)
			{
				MarkActorKnownToUser(userId, actorId);
				userState->baselineDelivery.erase(actorId);

				const entt::entity e = nw ? nw->ResolveActor(actorId) : entt::null;
				if (e != entt::null && m_world.valid(e) && m_world.all_of<OwnershipTag>(e))
				{
					if (m_world.get<OwnershipTag>(e).userId == userId && m_world.all_of<ClientRequestCorrelation>(e))
						m_world.remove<ClientRequestCorrelation>(e);
				}
			}
			else if (event.op == fb::fbLifecycleOp_Remove && event.reason == fb::fbRemovalReason_Destroyed)
			{
				ForgetActorForUser(userId, actorId);
			}
			else if (event.op == fb::fbLifecycleOp_Remove && event.reason == fb::fbRemovalReason_AoiLeft)
			{
				ForgetActorForUser(userId, actorId);
			}

			userState->pendingLifecycle.erase(pendingIt);
		}
	}

	void ServerReplicationSystem::InvalidateUserCaches(uint64 userId, ActorId actorId)
	{
		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		userState->baselineDelivery.erase(actorId);
	}

	void ServerReplicationSystem::InvalidateAllUserCaches(ActorId actorId)
	{
		if (auto it = m_knownUsersByActor.find(actorId); it != m_knownUsersByActor.end())
		{
			for (const KnownUserSlot& slot : it->second)
			{
				if (!slot.alive)
					continue;

				const uint64 userId = slot.userId;
				if (auto* userState = FindUserState(userId))
					userState->baselineDelivery.erase(actorId);
			}
		}
	}

	bool ServerReplicationSystem::IsActorKnownToUser(uint64 userId, ActorId actorId) const
	{
		if (userId == 0 || !actorId.IsValid())
			return false;

		if (const auto* userState = FindUserState(userId))
			return userState->knownActors.contains(actorId);
		return false;
	}

	void ServerReplicationSystem::MarkActorKnownToUser(uint64 userId, ActorId actorId)
	{
		if (userId == 0 || !actorId.IsValid())
			return;

		auto* userState = FindUserState(userId);
		if (!userState)
			return;
		userState->knownActors.insert(actorId);
		AddKnownUserToActor(actorId, userId);
	}

	void ServerReplicationSystem::ForgetActorForUser(uint64 userId, ActorId actorId)
	{
		if (userId == 0 || !actorId.IsValid())
			return;

		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		userState->knownActors.erase(actorId);
		userState->baselineDelivery.erase(actorId);
		RemoveKnownUserFromActor(actorId, userId);
	}

	void ServerReplicationSystem::ForgetActorForAllUsers(ActorId actorId)
	{
		if (!actorId.IsValid())
			return;

		for (auto& userState : m_userStates | std::views::values)
		{
			userState.knownActors.erase(actorId);
			userState.baselineDelivery.erase(actorId);
		}
		m_knownUsersByActor.erase(actorId);
	}

	bool ServerReplicationSystem::CanSendDelta(uint64 userId, ActorId actorId, uint32 fullEpoch) const
	{
		if (userId == 0 || !actorId.IsValid() || fullEpoch == 0)
			return false;

		const auto* userState = FindUserState(userId);
		if (!userState || userState->phase != eReplicationPhase::Streaming)
			return false;
		const entt::entity entity = m_netWorld ? m_netWorld->ResolveActor(actorId) : entt::null;
		if (entity != entt::null && m_world.valid(entity))
		{
			if (const auto* rigid = m_world.try_get<RigidAuthorityState>(entity);
				rigid && px::IsLocalDrivenKine(rigid->state.kineType))
				return true;
		}

		const auto it = userState->baselineDelivery.find(actorId);
		return it != userState->baselineDelivery.end()
			&& it->second.ackedBaselineRev == fullEpoch;
	}

	bool ServerReplicationSystem::ShouldSendFull(uint64 userId, ActorId actorId, uint32 fullEpoch)
	{
		auto* userState = FindUserState(userId);
		if (!userState)
			return false;

		auto& delivery = userState->baselineDelivery[actorId];
		if (fullEpoch != 0 && delivery.ackedBaselineRev == fullEpoch)
			return false;
		if (delivery.sentBaselineRev == 0)
			return true;
		if (delivery.sentBaselineRev != fullEpoch)
			return true;
		if ((m_tickCounter - delivery.lastSentTick) < kBaselineResendTicks)
			return false;
		if (delivery.resendCount >= kBaselineResendBudget)
		{
			if (userState->phase != eReplicationPhase::NeedsResync)
			{
				m_metrics->RecordBaselineResync();
				userState->phase = eReplicationPhase::NeedsResync;
			}
			return false;
		}
		return true;
	}

	void ServerReplicationSystem::TryCompleteInitialSync(uint64 userId)
	{
		auto* userState = FindUserState(userId);
		if (!userState || userState->phase != eReplicationPhase::InitialSync || !m_aoiSys)
			return;

		const auto* visibleActors = m_aoiSys->GetVisibleActors(userId);
		if (!visibleActors)
			return;

		for (const AoiVisibleActorSlot& slot : *visibleActors)
		{
			if (!slot.alive || slot.actor == entt::null || !m_world.valid(slot.actor) || !m_world.all_of<ActorId>(slot.actor))
				continue;
			const ActorId actorId = m_world.get<ActorId>(slot.actor);
			if (const auto* rigid = m_world.try_get<RigidAuthorityState>(slot.actor);
				rigid && px::IsLocalDrivenKine(rigid->state.kineType))
				continue;
			const ActorFrameCache* actorFrame = FindActorFrameCache(actorId);
			if (!actorFrame || actorFrame->isStatic || !actorFrame->canReplicate)
				continue;
			const uint32 currentEpoch = GetActorFullEpoch(slot.actor, actorId);
			const auto delivery = userState->baselineDelivery.find(actorId);
			if (delivery == userState->baselineDelivery.end()
				|| delivery->second.ackedBaselineRev != currentEpoch)
				return;
		}

		userState->phase = eReplicationPhase::Streaming;
	}

	void ServerReplicationSystem::MarkFullStateSent(uint64 userId, ActorId actorId)
	{
		if (userId == 0 || !actorId.IsValid())
			return;

		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		uint32 actorEpoch = 0;
		if (const auto it = m_sharedCharacterStates.find(actorId); it != m_sharedCharacterStates.end())
			actorEpoch = it->second.fullEpoch;
		else if (const auto it = m_sharedRigidStates.find(actorId); it != m_sharedRigidStates.end())
			actorEpoch = it->second.fullEpoch;

		if (actorEpoch != 0)
		{
			auto& delivery = userState->baselineDelivery[actorId];
			if (delivery.sentBaselineRev != actorEpoch)
			{
				delivery.sentBaselineRev = actorEpoch;
				delivery.ackedBaselineRev = 0;
				delivery.resendCount = 0;
			}
			else if (delivery.lastSentTick != 0)
			{
				m_metrics->RecordBaselineResend();
				++delivery.resendCount;
			}
			delivery.lastSentTick = m_tickCounter;
		}

	}
}
