#include "pch.h"
#include "jamnet/runtime/world/simulation/server/ServerReplicationSystem.h"
#include "jamnet/runtime/world/simulation/server/WorldMetrics.h"

#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"

#include "jamnet/runtime/world/simulation/server/ServerWorld.h"

#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/protocol/codec/ReplicationCodec.h"
#include "jamnet/runtime/protocol/codec/ActorCodec.h"


namespace jam::net
{
	namespace
	{
		constexpr size_t kLifecycleBatch = 96;
		constexpr size_t kSnapshotBatch = 128;
		constexpr size_t kPacketPayloadBudget = JAMNET_MTU - PacketHeader::HALF_SIZE - 24;
	}

	ServerReplicationSystem::ServerReplicationSystem(ServerWorld& world, entt::registry& registry, WorldMetrics& metrics)
		: m_world(world), m_registry(registry), m_metrics(metrics)
	{
		m_fbb.reset(new flatbuffers::FlatBufferBuilder(JAMNET_MTU));
	}

	void ServerReplicationSystem::Init()
	{
		m_userStates.clear();
		m_knownUsersByActor.clear();
		m_sharedRigidStates.clear();
		m_sharedCharacterStates.clear();
		m_actorFrameCache.clear();
		m_knownUsersScratch.clear();
		m_snapshotCandidatesScratch.clear();
		m_actorOffsScratch.clear();
		m_fullActorIdsScratch.clear();
		m_lifecycleActorOffsScratch.clear();
		m_lifecycleBatchScratch.clear();
		m_dirtyActorFrameScratch.clear();
		m_dirtyActorFrameDedup.clear();
		m_prevActiveActors.clear();
		m_currentActiveActorsScratch.clear();

		auto frameSeedView = m_registry.view<ActorId, ActorBodyType>(entt::exclude<ReplicationDisabledTag>);
		for (auto e : frameSeedView)
			MarkActorFrameDirty(e);

	}

	void ServerReplicationSystem::BeginTick(std::span<const entt::entity> activeEntities)
	{
		RefreshActorFrameCache(activeEntities);
	}

	// Snapshot contract: collect current-tick candidates in AOI order and choose
	// Full/Delta. Serialize and submit independent packets at size/count limits;
	// no complete-tick assembly or priority buckets are required.
	// Estimates cover Delta-to-Full fallback. Failed packets are not retained;
	// future ticks rebuild current state. Full delivery records and metrics
	// advance only after session job submission succeeds.
	void ServerReplicationSystem::ReplicateUser(const ReplicationUserInput& user, uint32 currentTick)
	{
		const uint64 userId = user.userId;
		auto* state = FindUserState(userId);
		if (!state) return;

		auto& userState = *state;
		if (userState.phase == eReplicationPhase::Suspended)
			return;
		if (userState.phase == eReplicationPhase::AwaitingPlayer)
		{
			EmitPendingLifecyclePackets(userId, currentTick);
			return;
		}

		if (userState.phase == eReplicationPhase::NeedsResync)
		{
			userState.baselineDelivery.clear();
			userState.phase = eReplicationPhase::InitialSync;
		}

		const uint32 ack		= user.appliedInputSequence;
		const uint32 inputEpoch = user.appliedControlRevision;

		QueueLifecycleForVisibleActors(userId, user.aoiState);
		EmitPendingLifecyclePackets(userId, currentTick);

		m_snapshotCandidatesScratch.clear();

		auto addCandidate = [&](entt::entity actor)
		{
			const ActorFrameCache* actorFrame = FindActorFrameCache(actor);
			if (!actorFrame)
				return;
			const ActorId actorId = actorFrame->actorId;
			if (!userState.activeActors.contains(actorId))
				return;

			const uint32 actorEpoch	= GetActorFullEpoch(actorId, actorFrame->e);

			const auto deliveryIt = userState.baselineDelivery.find(actorId);
			auto* delivery = deliveryIt != userState.baselineDelivery.end() ? &deliveryIt->second : nullptr;

			const bool useFull = !CanSendDelta(userState, actorFrame->e, delivery, actorEpoch) || user.aoiState->entered.contains(actorFrame->e);

			if (useFull)
			{
				if (!delivery)
					delivery = &userState.baselineDelivery.try_emplace(actorId).first->second;
				if (!ShouldSendFull(*delivery, actorEpoch, currentTick)) return;
			}
			else if (!actorFrame->isActive)
			{
				return;
			}

			const size_t sizeEstimate = actorFrame->sizeEstimate;
			if (sizeEstimate > kPacketPayloadBudget)
			{
				JAM_LOG_WARN("[Snapshot] single actor too large. user={}, actorId={}", userId, actorId.Value());
				return;
			}
			m_snapshotCandidatesScratch.push_back(Candidate{
				.e = actorFrame->e, .actorId = actorId, .useFull = useFull, .sizeEstimate = sizeEstimate
			});
		};


		if (const auto* aoiState = user.aoiState)
		{
			for (const entt::entity actor : aoiState->visible)
			{
				if (actor == entt::null || !m_registry.valid(actor))
					continue;
				addCandidate(actor);
			}
		}

		if (m_snapshotCandidatesScratch.empty())
		{
			TryCompleteInitialSync(userId, user.aoiState);
			return;
		}

		m_actorOffsScratch.reserve(kSnapshotBatch);
		m_fullActorIdsScratch.reserve(kSnapshotBatch);
		size_t cursor = 0;
		while (cursor < m_snapshotCandidatesScratch.size())
		{
			m_fbb->Clear();
			m_actorOffsScratch.clear();
			m_fullActorIdsScratch.clear();
			size_t usedPayloadBudget = 0;

			for (; cursor < m_snapshotCandidatesScratch.size(); ++cursor)
			{
				const Candidate& c = m_snapshotCandidatesScratch[cursor];
				if (m_actorOffsScratch.size() >= kSnapshotBatch || usedPayloadBudget + c.sizeEstimate > kPacketPayloadBudget)
					break;

				flatbuffers::Offset<fb::fbActorEntity> off = 0;
				bool builtFull = c.useFull;
				if (c.useFull)
					off = BuildFullActorEntity(userId, c.e, currentTick);
				else
					off = BuildDeltaActorEntity(userId, c.e, builtFull, currentTick);

				if (off.IsNull())
				{
					JAM_LOG_WARN("[Snapshot] actor serialization failed. user={}, actorId={}", userId, c.actorId.Value());
					return;
				}

				m_actorOffsScratch.push_back(off);
				usedPayloadBudget += c.sizeEstimate;
				if (builtFull)
					m_fullActorIdsScratch.push_back(c.actorId);
			}


			const auto header = fb::CreatefbSnapshotHeader(*m_fbb, currentTick, ack, inputEpoch);
			const auto vec	  = m_fbb->CreateVector(m_actorOffsScratch);
			const auto snap	  = fb::CreatefbSnapshot(*m_fbb, m_world.GetWorldId(), header, vec);
			m_fbb->Finish(snap, fb::fbSnapshotIdentifier());

			auto pkt = PacketBuilder::CreateCustomPacket(CustomPacketId::SNAPSHOT, PacketFlags::NONE, eChannel::UNRELIABLE_SEQUENCED, m_fbb->GetBufferPointer(), m_fbb->GetSize());
			if (!pkt.IsValid())
				continue;
			if (!m_world.TrySendTo(pkt, userId))
				continue;

			m_metrics.RecordSnapshotPacket(pkt->Size(), m_actorOffsScratch.size(), m_fullActorIdsScratch.size());
			for (const ActorId actorId : m_fullActorIdsScratch)
				MarkFullStateSent(userId, actorId, currentTick);
		}

		TryCompleteInitialSync(userId, user.aoiState);
	}

	void ServerReplicationSystem::EndTick()
	{
		uint64 pendingLifecycleTotal = 0;
		for (const auto& state : m_userStates | std::views::values)
			pendingLifecycleTotal += state.pendingLifecycle.size();
		m_metrics.SetLifecyclePending(pendingLifecycleTotal);
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


	uint32 ServerReplicationSystem::GetActorFullEpoch(ActorId actorId, entt::entity e)
	{
		if (e == entt::null || !m_registry.valid(e))
			return 0;

		const auto* body = m_registry.try_get<ActorBodyType>(e);
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

	void ServerReplicationSystem::RefreshActorFrameCache(std::span<const entt::entity> activeEntities)
	{
		m_currentActiveActorsScratch.clear();
		for (const entt::entity e : activeEntities)
			m_currentActiveActorsScratch.insert(e);

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

	const ServerReplicationSystem::ActorFrameCache* ServerReplicationSystem::FindActorFrameCache(entt::entity e) const
	{
		if (const auto it = m_actorFrameCache.find(e); it != m_actorFrameCache.end())
			return &it->second;
		return nullptr;
	}

	void ServerReplicationSystem::AddKnownUserToActor(uint64 userId, ActorId actorId)
	{
		auto* userState = FindUserState(userId);
		if (!userState || userState->knownActors.contains(actorId))
			return;

		auto& slots = m_knownUsersByActor[actorId];
		userState->knownActors.emplace(actorId, slots.size());
		slots.push_back(KnownUserSlot{ userId });
	}

	void ServerReplicationSystem::RemoveKnownUserFromActor(uint64 userId, ActorId actorId)
	{
		auto* userState = FindUserState(userId);
		if (!userState) return;
		const auto entry = userState->knownActors.find(actorId);
		if (entry == userState->knownActors.end()) return;

		auto it = m_knownUsersByActor.find(actorId);
		JAM_ASSERT(it != m_knownUsersByActor.end());
		if (it == m_knownUsersByActor.end()) return;

		auto& slots = it->second;
		const size_t index = entry->second;
		const bool indexValid = index < slots.size() && slots[index].userId == userId;
		JAM_ASSERT(indexValid);
		if (!indexValid) return;

		if (index != slots.size() - 1)
		{
			auto* movedUser = FindUserState(slots.back().userId);
			JAM_ASSERT(movedUser && movedUser->knownActors.contains(actorId));
			if (!movedUser || !movedUser->knownActors.contains(actorId)) return;
			slots[index] = slots.back();
			movedUser->knownActors.at(actorId) = index;
		}
		slots.pop_back();
		if (slots.empty())
			m_knownUsersByActor.erase(it);
		// The caller erases/clears knownActors after unlinking. This allows
		// full-sync/leave to iterate its keys without invalidating iterators.
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
		if (e == entt::null || !m_registry.valid(e) || !m_registry.all_of<ActorId, ActorBodyType>(e)
			|| m_registry.any_of<ReplicationDisabledTag, ReplicationStaticTag>(e))
		{
			m_actorFrameCache.erase(e);
			return;
		}

		const ActorId actorId = m_registry.get<ActorId>(e);
		const px::eBodyType bodyType = m_registry.get<ActorBodyType>(e).body;
		const bool canReplicate = (bodyType == px::eBodyType::Character && m_registry.all_of<CharAuthorityState>(e))
			|| (bodyType == px::eBodyType::Rigid && m_registry.all_of<RigidAuthorityState>(e));
		if (!actorId.IsValid() || !canReplicate)
		{
			m_actorFrameCache.erase(e);
			return;
		}

		ActorFrameCache frame{};
		frame.e = e;
		frame.actorId = actorId;
		frame.fullEpoch = GetActorFullEpoch(actorId, e);
		frame.bodyType = bodyType;
		frame.isActive = isActiveOverride;

		if (bodyType == px::eBodyType::Character)
		{
			frame.sizeEstimate = 48;
		}
		else
		{
			const auto& rs = m_registry.get<RigidAuthorityState>(e);
			frame.isKinematic = (rs.state.kineType != px::eKineDrivenType::None
				&& rs.state.kineType != px::eKineDrivenType::RuntimeDynamic);
			frame.sizeEstimate = frame.isKinematic ? 56 : 48;
		}

		m_actorFrameCache[e] = frame;
	}

	void ServerReplicationSystem::MarkActorDirty(entt::entity e)
	{
		if (!m_registry.valid(e) || !m_registry.all_of<ActorId>(e))
			return;

		const ActorId actorId = m_registry.get<ActorId>(e);
		if (!actorId.IsValid())
			return;

		MarkActorFrameDirty(e);
	}

	bool ServerReplicationSystem::AttachUser(uint64 userId)
	{
		if (userId == 0)
			return false;

		auto [it, inserted] = m_userStates.try_emplace(userId, ReplicationUserState
			{
				.phase = eReplicationPhase::Suspended,
			});
		if (!inserted)
			return it->second.phase == eReplicationPhase::Suspended;

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

	bool ServerReplicationSystem::ResumeUserWithFullSync(uint64 userId, bool playerReady)
	{
		auto* userState = FindUserState(userId);
		if (!userState || userState->phase != eReplicationPhase::Suspended)
			return false;

		// WorldResync creates a fresh client World, including an empty Meta cache.
		// Forget this user's introductions as well as baselines so visible actors
		// are introduced with Created again before Full snapshots are applied.
		for (const ActorId actorId : userState->knownActors | std::views::keys)
			RemoveKnownUserFromActor(userId, actorId);
		userState->knownActors.clear();
		userState->activeActors.clear();
		userState->pendingLifecycle.clear();
		userState->baselineDelivery.clear();
		userState->phase = playerReady ? eReplicationPhase::InitialSync : eReplicationPhase::AwaitingPlayer;
		return true;
	}

	void ServerReplicationSystem::AwaitPlayer(uint64 userId)
	{
		auto* userState = FindUserState(userId);
		if (!userState) return;

		std::erase_if(userState->pendingLifecycle, [](const auto& entry)
		{
			return entry.second == eActorLifecycleOp::Created || entry.second == eActorLifecycleOp::AoiEntered;
		});

		for (const ActorId actorId : userState->activeActors)
			QueueRemovalForUser(userId, actorId, eActorLifecycleOp::AoiLeft);

		userState->baselineDelivery.clear();
		if (userState->phase != eReplicationPhase::Suspended)
			userState->phase = eReplicationPhase::AwaitingPlayer;
	}

	bool ServerReplicationSystem::IsAwaitingPlayer(uint64 userId) const
	{
		const auto* userState = FindUserState(userId);
		return userState && userState->phase == eReplicationPhase::AwaitingPlayer;
	}

	bool ServerReplicationSystem::IsSuspended(uint64 userId) const
	{
		const auto* state = FindUserState(userId);
		return state && state->phase == eReplicationPhase::Suspended;
	}

	void ServerReplicationSystem::HandleBaselineFeedback(uint64 userId, const fb::fbBaselineAckBatch& batch, const UserAoiState* aoiState)
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
			if (!actorId.IsValid() || !userState->activeActors.contains(actorId))
				continue;

			const entt::entity entity = m_world.ResolveActor(actorId);
			if (entity == entt::null || !m_registry.valid(entity))
				continue;
			const uint32 currentEpoch = GetActorFullEpoch(actorId, entity);
			auto& delivery = userState->baselineDelivery[actorId];

			if (entry->request_full())
			{
				m_metrics.RecordBaselineFullRequest();
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

		TryCompleteInitialSync(userId, aoiState);
	}

	void ServerReplicationSystem::OnUserLeave(uint64 userId)
	{
		if (userId == 0)
			return;

		if (auto* userState = FindUserState(userId))
		{
			for (const ActorId actorId : userState->knownActors | std::views::keys)
				RemoveKnownUserFromActor(userId, actorId);
		}

		m_userStates.erase(userId);
	}

	void ServerReplicationSystem::OnActorDestroyed(entt::entity e)
	{
		if (!m_registry.valid(e) || !m_registry.all_of<ActorId>(e))
			return;

		const ActorId actorId = m_registry.get<ActorId>(e);
		if (!actorId.IsValid())
			return;

		InvalidateAllUserCaches(actorId);
		for (auto& userState : m_userStates | std::views::values)
		{
			if (!userState.knownActors.contains(actorId))
				userState.pendingLifecycle.erase(actorId);
		}

		if (auto it = m_knownUsersByActor.find(actorId); it != m_knownUsersByActor.end())
		{
			m_knownUsersScratch.clear();
			m_knownUsersScratch.reserve(it->second.size());
			for (const KnownUserSlot& slot : it->second)
			{
				m_knownUsersScratch.push_back(slot.userId);
			}

			for (const uint64 userId : m_knownUsersScratch)
				QueueRemovalForUser(userId, actorId, eActorLifecycleOp::Destroyed);
		}

		m_actorFrameCache.erase(e);
		m_prevActiveActors.erase(e);
		m_currentActiveActorsScratch.erase(e);
		m_dirtyActorFrameDedup.erase(e);
		m_sharedRigidStates.erase(actorId);
		m_sharedCharacterStates.erase(actorId);
	}

	flatbuffers::Offset<fb::fbActorMeta> ServerReplicationSystem::BuildActorMeta(uint64 userId, entt::entity e)
	{
		bool owned				= false;
		bool controlled			= false;
		uint64 actorArchetypeKey	= 0;
		const auto* body = m_registry.try_get<ActorBodyType>(e);
		if (!body || body->body == px::eBodyType::None)
			return 0;

		const uint8 bodyType = static_cast<uint8>(body->body);

		if (auto* o   = m_registry.try_get<OwnershipTag>(e))			owned			 = o->userId == userId;
		if (auto* c   = m_registry.try_get<ControlTag>(e))				controlled		 = c->userId == userId;
		if (auto* a   = m_registry.try_get<ActorArchetypeRef>(e))	actorArchetypeKey = a->key.v;

		return fb::CreatefbActorMeta(*m_fbb, owned, controlled, actorArchetypeKey, bodyType);
	}

	flatbuffers::Offset<fb::fbLifecycleActor> ServerReplicationSystem::BuildLifecycleActor(uint64 userId, eActorLifecycleOp op, ActorId actorId)
	{
		if (op != eActorLifecycleOp::Created)
			return fb::CreatefbLifecycleActor(*m_fbb, codec::EncodeActorLifecycleOp(op), actorId.Value());

		const entt::entity e = m_world.ResolveActor(actorId);
		if (e == entt::null || !m_registry.valid(e))
			return 0;

		const auto meta = BuildActorMeta(userId, e);
		if (meta.IsNull())
			return 0;

		return fb::CreatefbLifecycleActor(*m_fbb, codec::EncodeActorLifecycleOp(op), actorId.Value(), meta);
	}

	flatbuffers::Offset<fb::fbActorEntity> ServerReplicationSystem::BuildFullActorEntity(uint64 userId, entt::entity e, uint32 currentTick)
	{
		const ActorId actorId = m_registry.get<ActorId>(e);
		(void)userId;

		if (const auto* cs = m_registry.try_get<CharAuthorityState>(e))
		{
			const auto& state = cs->state;
			auto& sharedState = m_sharedCharacterStates[actorId];

			if (!sharedState.hasBaseline)
			{
				++sharedState.fullEpoch;
				sharedState.fullBuiltTick  = currentTick;
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

		if (const auto* rs = m_registry.try_get<RigidAuthorityState>(e))
		{
			const auto& state = rs->state;
			auto& sharedState = m_sharedRigidStates[actorId];

			if (px::IsLocalDrivenKine(state.kineType))
			{
				if (sharedState.fullBuiltTick != currentTick)
				{
					++sharedState.fullEpoch;
					sharedState.fullBuiltTick  = currentTick;
					sharedState.hasPackedDelta = false;
				}

				uint32 targetActorRaw = ActorId::Invalid().Value();
				if (state.kineState.targetActorId != px::INVALID_ACTOR_ID)
				{
					const entt::entity targetEntity = m_world.ResolveActor(ActorId(state.kineState.targetActorId));
					if (m_registry.valid(targetEntity) && m_registry.all_of<ActorId>(targetEntity))
						targetActorRaw = m_registry.get<ActorId>(targetEntity).Value();
				}

				const fb::fbKinematicState kine(state.kineState.startEpoch, state.kineState.phase, state.kineState.t, targetActorRaw, state.kineState.eventMask, static_cast<uint8>(state.kineType));
				return fb::CreatefbActorEntity(*m_fbb, actorId.Value(), sharedState.fullEpoch, nullptr, nullptr, nullptr, nullptr, &kine);
			}

			if (!sharedState.hasBaseline)
			{
				++sharedState.fullEpoch;
				sharedState.fullBuiltTick  = currentTick;
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

	flatbuffers::Offset<fb::fbActorEntity> ServerReplicationSystem::BuildDeltaActorEntity(uint64 userId, entt::entity e, OUT bool& builtFull, uint32 currentTick)
	{
		builtFull = false;
		const auto buildFull = [&]()
		{
			builtFull = true;
			return BuildFullActorEntity(userId, e, currentTick);
		};

		const ActorId actorId = m_registry.get<ActorId>(e);
		auto* userStatePtr = FindUserState(userId);
		if (!userStatePtr)
			return buildFull();

		if (const auto* cs = m_registry.try_get<CharAuthorityState>(e))
		{
			const auto& state = cs->state;
			auto& sharedState = m_sharedCharacterStates[actorId];
			if (!CanSendDelta(userId, actorId, sharedState.fullEpoch) || !sharedState.hasBaseline)
				return buildFull();

			if (!sharedState.hasPackedDelta || sharedState.lastDeltaSourceState != state)
			{
				if (!PackCharacterDelta128(sharedState.baseline.pos, sharedState.baseline.bodyYaw, sharedState.baseline.viewYaw, sharedState.baseline.pitch, state, sharedState.packedDelta))
				{
					sharedState.hasBaseline = false;
					return buildFull();
				}
				sharedState.lastDeltaSourceState = state;
				sharedState.hasPackedDelta = true;
			}

			const fb::fbCharacterDelta128 charDelta(sharedState.packedDelta.Word(0), sharedState.packedDelta.Word(1));
			return fb::CreatefbActorEntity(*m_fbb, actorId.Value(), sharedState.fullEpoch, nullptr, nullptr, nullptr, &charDelta);
		}

		if (auto* rs = m_registry.try_get<RigidAuthorityState>(e))
		{
			auto& state = rs->state;
			auto& sharedState = m_sharedRigidStates[actorId];
			if (!CanSendDelta(userId, actorId, sharedState.fullEpoch))
				return buildFull();

			if (px::IsLocalDrivenKine(state.kineType))
			{
				if (state.kineState.startEpoch == 0)
					state.kineState.startEpoch = currentTick;

				uint32 targetActorRaw = ActorId::Invalid().Value();
				if (state.kineState.targetActorId != px::INVALID_ACTOR_ID)
				{
					const entt::entity targetEntity = m_world.ResolveActor(ActorId(state.kineState.targetActorId));
					if (m_registry.valid(targetEntity) && m_registry.all_of<ActorId>(targetEntity))
						targetActorRaw = m_registry.get<ActorId>(targetEntity).Value();
				}

				const fb::fbKinematicState kine(state.kineState.startEpoch, state.kineState.phase, state.kineState.t, targetActorRaw, state.kineState.eventMask, E2U(state.kineType));
				return fb::CreatefbActorEntity(*m_fbb, actorId.Value(), sharedState.fullEpoch, nullptr, nullptr, nullptr, nullptr, &kine);
			}

			if (!sharedState.hasBaseline)
				return buildFull();

			if (!sharedState.hasPackedDelta || sharedState.lastDeltaSourceState != state)
			{
				if (!PackRigidDelta128(sharedState.baseline.pos, sharedState.baseline.rot, state, sharedState.packedDelta))
				{
					sharedState.hasBaseline = false;
					return buildFull();
				}
				sharedState.lastDeltaSourceState = state;
				sharedState.hasPackedDelta = true;
			}

			const fb::fbTransformDelta rigidDelta(sharedState.packedDelta.Word(0), sharedState.packedDelta.Word(1));
			return fb::CreatefbActorEntity(*m_fbb, actorId.Value(), sharedState.fullEpoch, nullptr, &rigidDelta);

		}

		return 0;
	}

	void ServerReplicationSystem::QueueLifecycleEnterForUser(uint64 userId, ActorId actorId)
	{
		auto* userState = FindUserState(userId);
		if (!userState || !actorId.IsValid()) return;

		auto& pending = userState->pendingLifecycle;
		if (const auto it = pending.find(actorId); it != pending.end())
		{
			if (it->second == eActorLifecycleOp::Destroyed)
				return;
			if (it->second == eActorLifecycleOp::AoiLeft)
				pending.erase(it);
		}
		if (userState->activeActors.contains(actorId)) return;

		pending[actorId] = userState->knownActors.contains(actorId) ? eActorLifecycleOp::AoiEntered : eActorLifecycleOp::Created;
		InvalidateUserCaches(userId, actorId);
	}

	void ServerReplicationSystem::QueueRemovalForUser(uint64 userId, ActorId actorId, eActorLifecycleOp op)
	{
		auto* userState = FindUserState(userId);
		if (!userState || !actorId.IsValid()) return;

		auto& pending = userState->pendingLifecycle;
		if (const auto it = pending.find(actorId); it != pending.end() && it->second == eActorLifecycleOp::Destroyed)
			return;

		if (!userState->knownActors.contains(actorId) || (op == eActorLifecycleOp::AoiLeft && !userState->activeActors.contains(actorId)))
		{
			pending.erase(actorId);
			return;
		}

		pending[actorId] = op;
		InvalidateUserCaches(userId, actorId);
	}

	void ServerReplicationSystem::QueueLifecycleForVisibleActors(uint64 userId, const UserAoiState* state)
	{
		auto* userState = FindUserState(userId);
		if (!userState) return;
		if (!state)
		{
			std::erase_if(userState->pendingLifecycle, [](const auto& entry)
			{
				return entry.second == eActorLifecycleOp::Created || entry.second == eActorLifecycleOp::AoiEntered;
			});
			for (const ActorId id : userState->activeActors)
				QueueRemovalForUser(userId, id, eActorLifecycleOp::AoiLeft);
			return;
		}

		std::erase_if(userState->pendingLifecycle, [&](const auto& entry)
		{
			if (entry.second != eActorLifecycleOp::Created && entry.second != eActorLifecycleOp::AoiEntered)
				return false;
			const entt::entity actor = m_world.ResolveActor(entry.first);
			return actor == entt::null || !state->visible.contains(actor);
		});

		for (const ActorId id : state->left)
			QueueRemovalForUser(userId, id, eActorLifecycleOp::AoiLeft);

		for (const entt::entity actor : state->entered)
		{
			if (state->visible.contains(actor) && m_registry.valid(actor) && m_registry.all_of<ActorId>(actor))
				QueueLifecycleEnterForUser(userId, m_registry.get<ActorId>(actor));
		}

		if (userState->phase != eReplicationPhase::InitialSync) return;

		// Reconcile AOI activation without discarding lifetime knowledge of Meta.
		for (const ActorId id : userState->activeActors)
		{
			const entt::entity actor = m_world.ResolveActor(id);
			if (actor == entt::null || !state->visible.contains(actor))
				QueueRemovalForUser(userId, id, eActorLifecycleOp::AoiLeft);
		}
		for (const entt::entity actor : state->visible)
		{
			if (m_registry.valid(actor) && m_registry.all_of<ActorId>(actor))
				QueueLifecycleEnterForUser(userId, m_registry.get<ActorId>(actor));
		}
	}


	// Lifecycle submission contract:
	// - For a sendable user, attempt every pending event in this tick, splitting
	//   packets by batch/payload limits; there is no per-tick submission budget.
	// - Commit only batches whose session job submission succeeds. This confirms
	//   handoff, not delivery; reliable retransmission belongs to the transport.
	// - On serialization/packet/submission failure, stop submitting later batches
	//   and preserve all uncommitted events for the next tick.
	// - Suspended users retain pending events (including Destroyed outside AOI).
	//   Full-sync resume resets knowledge and introduces the current visible actors again.
	// - Owning-shard execution does not yield or reenter lifecycle submission.
	//   The pending map is not inserted into or rehashed during iteration; commit
	//   erases only submitted entries, preserving all other iterators.
	void ServerReplicationSystem::EmitPendingLifecyclePackets(uint64 userId, uint32 tick)
	{
		auto* userState = FindUserState(userId);
		if (!userState || userState->pendingLifecycle.empty())
			return;

		auto& pending    = userState->pendingLifecycle;
		auto& actorOffs  = m_lifecycleActorOffsScratch;
		auto& sentEvents = m_lifecycleBatchScratch;
		actorOffs.reserve(kLifecycleBatch);
		sentEvents.reserve(kLifecycleBatch);

		auto estimateEventBytes = [](eActorLifecycleOp op) -> size_t
		{
			switch (op)
			{
			case eActorLifecycleOp::Created: return 72;
			case eActorLifecycleOp::AoiEntered:
			case eActorLifecycleOp::AoiLeft:
			case eActorLifecycleOp::Destroyed: return 20;
			default: return 32;
			}
		};

		auto cursor = pending.begin();
		while (cursor != pending.end())
		{
			m_fbb->Clear();

			actorOffs.clear();
			sentEvents.clear();

			size_t usedPayloadBudget = 0;
			for (; cursor != pending.end(); ++cursor)
			{
				if (actorOffs.size() >= kLifecycleBatch)
					break;

				const auto& [actorId, event] = *cursor;
				const size_t est = estimateEventBytes(event);
				if (est > kPacketPayloadBudget)
				{
					JAM_LOG_WARN("[LifecycleTx] event exceeds payload budget. userId={}, actorId={}", userId, actorId.Value());
					return;
				}

				if ((usedPayloadBudget + est) > kPacketPayloadBudget && !actorOffs.empty())
				{
					m_metrics.RecordLifecyclePacketSplit();
					break;
				}

				const auto off = BuildLifecycleActor(userId, event, actorId);
				if (off.IsNull())
				{
					JAM_LOG_WARN("[LifecycleTx] event build failed. userId={}, actorId={}", userId, actorId.Value());
					return;
				}

				actorOffs.push_back(off);
				sentEvents.push_back(cursor);
				usedPayloadBudget += est;
			}

			if (actorOffs.empty())
				break;

			const auto vec = m_fbb->CreateVector(actorOffs);
			const auto batch = fb::CreatefbLifecycleBatch(*m_fbb, m_world.GetWorldId(), tick, vec);
			m_fbb->Finish(batch, fb::fbLifecycleBatchIdentifier());

			auto pkt = PacketBuilder::CreateCustomPacket(CustomPacketId::LIFECYCLE, PacketFlags::NONE, eChannel::RELIABLE_ORDERED, m_fbb->GetBufferPointer(), m_fbb->GetSize());
			if (!pkt.IsValid())
			{
				JAM_LOG_WARN("[LifecycleTx] packet build failed. userId={}, worldId={}, tick={}, actors={}, payloadBytes={}",
					userId, m_world.GetWorldId(), tick, actorOffs.size(), m_fbb->GetSize());
				return;
			}

			if (!m_world.TrySendTo(pkt, userId))
			{
				JAM_LOG_WARN("[LifecycleTx] session job submission failed. userId={}, worldId={}, tick={}, actors={}",
					userId, m_world.GetWorldId(), tick, sentEvents.size());
				return;
			}
			m_metrics.RecordLifecyclePacket(pkt->Size(), sentEvents.size());

			CommitPendingLifecycleBatch(userId, sentEvents);
			sentEvents.clear();
		}
	}

	void ServerReplicationSystem::CommitPendingLifecycleBatch(uint64 userId, std::span<const ReplicationUserState::PendingLifecycleMap::iterator> sentEvents)
	{
		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		for (const auto pendingIt : sentEvents)
		{
			const auto& [actorId, event] = *pendingIt;

			if (event == eActorLifecycleOp::Created || event == eActorLifecycleOp::AoiEntered)
			{
				if (event == eActorLifecycleOp::Created)
					MarkActorKnownToUser(userId, actorId);
				userState->activeActors.insert(actorId);
				userState->baselineDelivery.erase(actorId);
			}
			else if (event == eActorLifecycleOp::Destroyed)
			{
				ForgetActorForUser(userId, actorId);
			}
			else if (event == eActorLifecycleOp::AoiLeft)
			{
				userState->activeActors.erase(actorId);
				userState->baselineDelivery.erase(actorId);
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
				const uint64 userId = slot.userId;
				if (auto* userState = FindUserState(userId))
					userState->baselineDelivery.erase(actorId);
			}
		}
	}

	void ServerReplicationSystem::MarkActorKnownToUser(uint64 userId, ActorId actorId)
	{
		if (userId == 0 || !actorId.IsValid())
			return;

		auto* userState = FindUserState(userId);
		if (!userState)
			return;
		AddKnownUserToActor(userId, actorId);
	}

	void ServerReplicationSystem::ForgetActorForUser(uint64 userId, ActorId actorId)
	{
		if (userId == 0 || !actorId.IsValid())
			return;

		auto* userState = FindUserState(userId);
		if (!userState)
			return;

		RemoveKnownUserFromActor(userId, actorId);
		userState->knownActors.erase(actorId);
		userState->activeActors.erase(actorId);
		userState->baselineDelivery.erase(actorId);
	}



	bool ServerReplicationSystem::CanSendDelta(uint64 userId, ActorId actorId, uint32 fullEpoch) const
	{
		if (userId == 0 || !actorId.IsValid() || fullEpoch == 0)
			return false;

		const auto* userState = FindUserState(userId);
		if (!userState)
			return false;
		entt::entity entity = m_world.ResolveActor(actorId);
		if (entity != entt::null && !m_registry.valid(entity))
			entity = entt::null;
		const auto it = userState->baselineDelivery.find(actorId);
		return CanSendDelta(*userState, entity,
			it != userState->baselineDelivery.end() ? &it->second : nullptr, fullEpoch);
	}

	bool ServerReplicationSystem::CanSendDelta(const ReplicationUserState& userState, entt::entity entity,
		const ReplicationUserState::BaselineDeliveryState* delivery, uint32 fullEpoch) const
	{
		if (fullEpoch == 0 || (userState.phase != eReplicationPhase::InitialSync
			&& userState.phase != eReplicationPhase::Streaming))
			return false;
		if (entity != entt::null)
		{
			if (const auto* rigid = m_registry.try_get<RigidAuthorityState>(entity);
				rigid && px::IsLocalDrivenKine(rigid->state.kineType))
				return true;
		}

		return delivery && delivery->ackedBaselineRev == fullEpoch;
	}

	bool ServerReplicationSystem::ShouldSendFull(ReplicationUserState::BaselineDeliveryState& delivery,
		uint32 fullEpoch, uint32 currentTick)
	{
		if (fullEpoch != 0 && delivery.ackedBaselineRev == fullEpoch)
			return false;
		if (delivery.sentBaselineRev == 0)
			return true;
		if (delivery.sentBaselineRev != fullEpoch)
			return true;
		if ((currentTick - delivery.lastSentTick) < kBaselineResendTicks)
			return false;
		if (delivery.resendCount >= kBaselineResendBudget)
		{
			m_metrics.RecordBaselineResync();
			delivery = {};
			return true;
		}
		return true;
	}

	void ServerReplicationSystem::TryCompleteInitialSync(uint64 userId, const UserAoiState* aoiState)
	{
		auto* userState = FindUserState(userId);
		if (!userState || userState->phase != eReplicationPhase::InitialSync)
			return;

		if (!aoiState)
			return;

		for (const entt::entity actor : aoiState->visible)
		{
			if (actor == entt::null || !m_registry.valid(actor) || !m_registry.all_of<ActorId>(actor))
				continue;
			const ActorId actorId = m_registry.get<ActorId>(actor);
			if (!userState->activeActors.contains(actorId))
				return;
			if (const auto* rigid = m_registry.try_get<RigidAuthorityState>(actor);
				rigid && px::IsLocalDrivenKine(rigid->state.kineType))
				continue;
			const ActorFrameCache* actorFrame = FindActorFrameCache(actor);
			if (!actorFrame)
				continue;
			const uint32 currentEpoch = GetActorFullEpoch(actorId, actor);
			const auto delivery = userState->baselineDelivery.find(actorId);
			if (delivery == userState->baselineDelivery.end()
				|| delivery->second.ackedBaselineRev != currentEpoch)
				return;
		}

		userState->phase = eReplicationPhase::Streaming;
	}

	void ServerReplicationSystem::MarkFullStateSent(uint64 userId, ActorId actorId, uint32 currentTick)
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
				m_metrics.RecordBaselineResend();
				++delivery.resendCount;
			}
			delivery.lastSentTick = currentTick;
		}

	}
}
