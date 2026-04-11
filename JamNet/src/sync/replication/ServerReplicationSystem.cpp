#include "pch.h"
#include "jamnet/sync/replication/ServerReplicationSystem.h"

#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationUtils.h"
#include "jamnet/sync/replication/ServerInputSystem.h"

#include "jamnet/sync/networld/ServerNetWorld.h"
#include "jamnet/sync/replication/NetWorldContext.h"
#include "jamnet/sync/replication/ServerAoiSystem.h"

#include "jamnet/sync/transport/CustomPacketHelper.h"

namespace jam::net
{
	namespace
	{
		constexpr size_t kLifecycleBatch = 96;
		constexpr size_t kSnapshotBatch = 128;
		constexpr size_t kPacketPayloadBudget = JAMNET_MTU - PacketHeader::HALF_SIZE - 24;
		constexpr uint8 kCreateFullStateBudget = 5;
	}

	ServerReplicationSystem::ServerReplicationSystem(entt::registry& world)
		: m_world(world)
	{
		m_fbb.reset(new flatbuffers::FlatBufferBuilder(JAMNET_MTU));
	}

	void ServerReplicationSystem::Init()
	{
		m_tickCounter = 0;

		m_entityBaselinePerUser.clear();
		m_rigidBaselineStatesPerUser.clear();
		m_characterBaselineStatesPerUser.clear();
		m_kineBaselineStatesPerUser.clear();

		m_cachedRigidDeltaPerUser.clear();
		m_cachedCharacterDeltaPerUser.clear();

		m_knownActorsPerUser.clear();
		m_pendingLifecyclePerUser.clear();
		m_forceLifecycleSyncPerUsers.clear();
		m_forceFullStateBudgetPerUser.clear();

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
		auto* nw = m_world.ctx().get<ServerNetWorld*>();
		if (!nw) return;

		auto* aoi = m_world.ctx().find<ServerAoiSystem>();

		if (m_fullCacheTick != m_tickCounter)
		{
			m_fullCacheTick = m_tickCounter;
			m_cachedRigidFull.clear();
			m_cachedCharacterFull.clear();
		}

		const bool periodicFull = ((m_tickCounter % kFullIntervalTicks) == 0);
		const uint32 tick = m_world.ctx().get<TickCounter>().tick;

		std::vector<uint64> users;
		nw->GetMembers(users);
		if (users.empty()) return;

		for (const uint64 user : users)
		{
			const uint32 ack = m_world.ctx().get<ServerInputSystem>().LastAppliedSeq(user);
			const uint32 inputEpoch = m_world.ctx().get<ServerInputSystem>().LastAppliedCommandEpoch(user);

			std::unordered_set<NetId> sentThisTick;
			sentThisTick.reserve(256);

			bool forceSyncUser = false;
			if (auto it = m_forceLifecycleSyncPerUsers.find(user); it != m_forceLifecycleSyncPerUsers.end() && it->second > 0)
				forceSyncUser = true;

			std::unordered_set<uint32> enteredSet;
			if (aoi)
			{
				if (const UserAoiState* st = aoi->GetState(user))
				{
					enteredSet.reserve(st->entered.size());
					for (const NetId id : st->entered)
						enteredSet.insert(id.Raw());
				}
			}

			QueueLifecycleForVisibleActors(user, aoi, forceSyncUser);
			EmitPendingLifecyclePackets(*nw, user, tick);

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

			std::array<std::vector<Candidate>, static_cast<size_t>(eBucket::Count)> buckets;
			auto view = m_world.view<NetId, NetActorBodyType>();

			for (auto e : view)
			{
				const NetId nid = m_world.get<NetId>(e);

				if (m_world.all_of<ReplicationStaticTag>(e))
					continue;

				if (aoi && !aoi->IsVisible(user, nid))
					continue;

				const bool baselineInvalid = ShouldForceFullState(user, nid)
					|| (!m_entityBaselinePerUser.contains(user))
					|| (!m_entityBaselinePerUser[user].contains(nid));
				const bool enteredNow = enteredSet.contains(nid.Raw());

				Candidate c{ .e = e, .netId = nid };

				if (baselineInvalid || enteredNow)
				{
					c.useFull = true;
					buckets[static_cast<size_t>(eBucket::B0_MustSendFull)].push_back(c);
					continue;
				}

				if (periodicFull)
				{
					c.useFull = true;
					buckets[static_cast<size_t>(eBucket::B0_MustSendFull)].push_back(c);
					continue;
				}

				if (!m_world.all_of<ReplicationActiveTag>(e))
				{
					buckets[static_cast<size_t>(eBucket::B3_LowPriority)].push_back(c);
					continue;
				}

				const auto body = m_world.get<NetActorBodyType>(e).body;
				if (body == px::eBodyType::Character)
					buckets[static_cast<size_t>(eBucket::B1_HighDelta)].push_back(c);
				else
					buckets[static_cast<size_t>(eBucket::B2_NormalDelta)].push_back(c);
			}

			std::vector<Candidate> ordered;
			ordered.reserve(buckets[0].size() + buckets[1].size() + buckets[2].size() + buckets[3].size());

			auto appendAll = [&](eBucket bucket)
			{
				auto& src = buckets[static_cast<size_t>(bucket)];
				ordered.insert(ordered.end(), src.begin(), src.end());
			};

			appendAll(eBucket::B0_MustSendFull);
			appendAll(eBucket::B1_HighDelta);
			appendAll(eBucket::B2_NormalDelta);

			if (ordered.empty())
				continue;

			auto estimateActorBytes = [&](const Candidate& c) -> size_t
			{
				size_t bytes = 24;
				const auto body = m_world.get<NetActorBodyType>(c.e).body;

				if (c.useFull)
				{
					if (body == px::eBodyType::Character)
					{
						bytes += 24;
					}
					else
					{
						const auto& rs = m_world.get<px::RigidState>(c.e);
						const bool isKine = (rs.kineType != px::eKineDrivenType::None &&
							rs.kineType != px::eKineDrivenType::RuntimeDynamic);
						bytes += isKine ? 32 : 24;
					}
				}
				else
				{
					if (body == px::eBodyType::Character)
					{
						bytes += 16;
					}
					else
					{
						const auto& rs = m_world.get<px::RigidState>(c.e);
						const bool isKine = (rs.kineType != px::eKineDrivenType::RuntimeDynamic);
						bytes += isKine ? 32 : 16;
					}
				}

				return bytes;
			};

			size_t cursor = 0;
			while (cursor < ordered.size())
			{
				m_fbb->Clear();

				std::vector<flatbuffers::Offset<fb::fbActorEntity>> offs;
				offs.reserve(kSnapshotBatch);

				size_t usedPayloadBudget = 0;
				const size_t beginCursor = cursor;

				for (; cursor < ordered.size(); ++cursor)
				{
					const Candidate& c = ordered[cursor];

					if (offs.size() >= kSnapshotBatch)
						break;

					const size_t est = estimateActorBytes(c);
					if (est > kPacketPayloadBudget)
					{
						JAMNET_LOG_WARN("[Snapshot] single actor too large. user={}, netId={}", user, c.netId.Raw());
						continue;
					}

					if ((usedPayloadBudget + est) > kPacketPayloadBudget)
					{
						if (!offs.empty())
							break;
						continue;
					}

					flatbuffers::Offset<fb::fbActorEntity> off = 0;
					if (c.useFull)
						off = BuildFullActorEntity(c.e, user);
					else
						off = BuildDeltaActorEntity(c.e, user);

					if (off.IsNull())
						continue;

					if (sentThisTick.contains(c.netId))
						continue;

					sentThisTick.insert(c.netId);
					offs.push_back(off);
					usedPayloadBudget += est;

					if (c.useFull)
						MarkFullStateSent(user, c.netId);
				}

				if (offs.empty())
				{
					if (cursor == beginCursor)
						++cursor;
					continue;
				}

				const auto header = fb::CreatefbSnapshotHeader(*m_fbb, tick, ack, inputEpoch);
				const auto vec = m_fbb->CreateVector(offs);
				const auto snap = fb::CreatefbSnapshot(*m_fbb, header, vec);
				m_fbb->Finish(snap, fb::fbSnapshotIdentifier());

				auto buf = PacketBuilder::CreateCustomPacket(
					CustomPacketId::SNAPSHOT,
					PacketFlags::NONE,
					eChannelType::UNRELIABLE_SEQUENCED,
					m_fbb->GetBufferPointer(),
					m_fbb->GetSize());

				if (!buf)
					continue;

				TransportInfo info{};
				info.method = eTransportMethod::Single;
				info.userId = user;
				nw->Send(info, buf);
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

	void ServerReplicationSystem::MarkActorDirty(entt::entity e, bool forceMeta)
	{
		if (!m_world.valid(e) || !m_world.all_of<NetId>(e))
			return;

		const NetId netId = m_world.get<NetId>(e);
		if (!netId.IsValid())
			return;

		InvalidateAllUserCaches(netId);

		if (!forceMeta)
			return;

		auto* nw = m_world.ctx().get<ServerNetWorld*>();
		if (!nw)
			return;

		std::vector<uint64> users;
		nw->GetMembers(users);

		for (const uint64 userId : users)
		{
			if (IsActorKnownToUser(userId, netId))
				QueueLifecycleMetaForUser(userId, netId);
			else
				QueueLifecycleCreateForUser(userId, netId);
		}
	}

	void ServerReplicationSystem::OnEnter(uint64 userId)
	{
		if (userId == 0)
			return;

		JAMNET_LOG_DEBUG("[ServerReplicationSystem] OnEnter() : userId= {}", userId);
		ForceLifecycleSyncForUser(userId, 5);
	}

	void ServerReplicationSystem::OnLeave(uint64 userId)
	{
		if (userId == 0)
			return;

		m_cachedRigidDeltaPerUser.erase(userId);
		m_cachedCharacterDeltaPerUser.erase(userId);

		m_entityBaselinePerUser.erase(userId);
		m_rigidBaselineStatesPerUser.erase(userId);
		m_characterBaselineStatesPerUser.erase(userId);
		m_kineBaselineStatesPerUser.erase(userId);

		m_forceLifecycleSyncPerUsers.erase(userId);
		m_pendingLifecyclePerUser.erase(userId);
		m_forceFullStateBudgetPerUser.erase(userId);

		std::erase_if(m_knownActorsPerUser, [userId](const ActorUserKey& key) { return key.userId == userId; });
	}

	void ServerReplicationSystem::OnActorDestroyed(entt::entity e)
	{
		if (!m_world.valid(e) || !m_world.all_of<NetId>(e))
			return;

		const NetId netId = m_world.get<NetId>(e);
		if (!netId.IsValid())
			return;

		InvalidateAllUserCaches(netId);

		std::vector<uint64> users;
		if (auto* nw = m_world.ctx().get<ServerNetWorld*>(); nw)
			nw->GetMembers(users);

		for (const uint64 userId : users)
			QueueRemovalForUser(userId, netId, fb::fbRemovalReason_Destroyed);

		m_cachedRigidFull.erase(netId);
		m_cachedCharacterFull.erase(netId);
	}

	flatbuffers::Offset<fb::fbActorMeta> ServerReplicationSystem::BuildActorMeta(entt::entity e, uint64 userId)
	{
		uint64 owner = 0;
		uint64 controller = 0;
		uint64 prefab = 0;
		uint32 spawnReqId = 0;
		uint32 packedId = 0;

		if (auto* o = m_world.try_get<OwnershipTag>(e)) owner = o->userId;
		if (auto* c = m_world.try_get<ControlTag>(e)) controller = c->userId;
		if (auto* p = m_world.try_get<NetPrefabKey>(e)) prefab = p->key.value;
		if (auto* tpr = m_world.try_get<NetTeamPartRole>(e)) packedId = tpr->Packed();

		if (userId != 0 && userId == owner)
		{
			if (auto* s = m_world.try_get<NetSpawnRequestId>(e))
				spawnReqId = s->requestId;
		}

		return fb::CreatefbActorMeta(*m_fbb, owner, controller, prefab, spawnReqId, packedId);
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
		const auto bodyType = m_world.get<NetActorBodyType>(e).body;

		auto& userEntityBase = m_entityBaselinePerUser[userId];

		if (bodyType == px::eBodyType::Character)
		{
			auto& cs = m_world.get<px::CharacterState>(e);

			PackedCharacterFull160 packed{};
			if (auto it = m_cachedCharacterFull.find(netId); it != m_cachedCharacterFull.end())
			{
				packed = it->second;
			}
			else
			{
				if (!PackCharacterFull160(cs, packed))
					return 0;
				m_cachedCharacterFull.emplace(netId, packed);
			}

			{
				auto& bs = m_characterBaselineStatesPerUser[userId][netId];
				bs.pos = cs.pos;
				bs.yaw = cs.facingYaw;
				bs.pitch = cs.facingPitch;
			}

			const fb::fbCharacterFull160 charFull(packed.data0, packed.data1, packed.data2);
			uint32& base = userEntityBase.try_emplace(netId, 0u).first->second;
			return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base, nullptr, nullptr, &charFull, nullptr, nullptr);
		}

		auto& rs = m_world.get<px::RigidState>(e);

		if (rs.kineType != px::eKineDrivenType::None && rs.kineType != px::eKineDrivenType::RuntimeDynamic)
		{
			auto& kineBs = m_kineBaselineStatesPerUser[userId][netId];
			kineBs = rs.kineState;

			uint32 targetNetRaw = NetId::Invalid().Raw();
			if (rs.kineState.targetId != px::INVALID_OBJ_ID)
			{
				const entt::entity targetEntity = static_cast<entt::entity>(rs.kineState.targetId);
				if (m_world.valid(targetEntity) && m_world.all_of<NetId>(targetEntity))
					targetNetRaw = m_world.get<NetId>(targetEntity).Raw();
			}

			const fb::fbKinematicState kine(
				rs.kineState.startEpoch,
				rs.kineState.phase,
				rs.kineState.t,
				targetNetRaw,
				rs.kineState.eventMask,
				static_cast<uint8>(rs.kineType));

			uint32& base = userEntityBase.try_emplace(netId, 0u).first->second;
			return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base, nullptr, nullptr, nullptr, nullptr, &kine);
		}

		PackedRigidFull192 packed{};
		if (auto it = m_cachedRigidFull.find(netId); it != m_cachedRigidFull.end())
		{
			packed = it->second;
		}
		else
		{
			if (!PackRigidFull192(rs, packed))
				return 0;
			m_cachedRigidFull.emplace(netId, packed);
		}

		{
			auto& bs = m_rigidBaselineStatesPerUser[userId][netId];
			bs.pos = rs.pose.p;
			bs.rot = rs.pose.q;
		}

		fb::fbTransformFull rigidFull(packed.data0, packed.data1, packed.data2);
		uint32& base = userEntityBase.try_emplace(netId, 0u).first->second;
		return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base, &rigidFull);
	}

	flatbuffers::Offset<fb::fbActorEntity> ServerReplicationSystem::BuildDeltaActorEntity(entt::entity e, uint64 userId)
	{
		const NetId netId = m_world.get<NetId>(e);
		const auto bodyType = m_world.get<NetActorBodyType>(e).body;

		auto& userEntityBase = m_entityBaselinePerUser[userId];

		if (bodyType == px::eBodyType::Character)
		{
			auto& cs = m_world.get<px::CharacterState>(e);

			auto& userCharBaseline = m_characterBaselineStatesPerUser[userId];
			auto bs = userCharBaseline.find(netId);
			if (bs == userCharBaseline.end())
				return BuildFullActorEntity(e, userId);

			PackedCharacterDelta128 packedDelta{};
			if (!PackCharacterDelta128(bs->second.pos, bs->second.yaw, bs->second.pitch, cs, packedDelta))
				return BuildFullActorEntity(e, userId);

			auto& userCache = m_cachedCharacterDeltaPerUser[userId];
			if (auto it = userCache.find(netId); it != userCache.end())
			{
				if (it->second.data0 == packedDelta.data0 && it->second.data1 == packedDelta.data1)
					return 0;
			}

			userCache[netId] = packedDelta;

			const fb::fbCharacterDelta128 charDelta(packedDelta.data0, packedDelta.data1);
			uint32& base = userEntityBase[netId];

			{
				auto& newBs = userCharBaseline[netId];
				newBs.pos = cs.pos;
				newBs.yaw = cs.facingYaw;
				newBs.pitch = cs.facingPitch;
			}

			return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base++, nullptr, nullptr, nullptr, &charDelta);
		}

		auto& userRigidBaseline = m_rigidBaselineStatesPerUser[userId];
		auto bs = userRigidBaseline.find(netId);
		if (bs == userRigidBaseline.end())
			return BuildFullActorEntity(e, userId);

		auto& rs = m_world.get<px::RigidState>(e);

		if (rs.kineType != px::eKineDrivenType::RuntimeDynamic)
		{
			if (rs.kineState.startEpoch == 0)
				rs.kineState.startEpoch = m_world.ctx().get<TickCounter>().tick;

			auto& userKineBaseline = m_kineBaselineStatesPerUser[userId];
			auto itBs = userKineBaseline.find(netId);
			if (itBs == userKineBaseline.end())
				return BuildFullActorEntity(e, userId);

			if (itBs->second == rs.kineState)
				return 0;

			itBs->second = rs.kineState;

			uint32& base = userEntityBase[netId];

			uint32 targetNetRaw = NetId::Invalid().Raw();
			if (rs.kineState.targetId != px::INVALID_OBJ_ID)
			{
				const entt::entity targetEntity = static_cast<entt::entity>(rs.kineState.targetId);
				if (m_world.valid(targetEntity) && m_world.all_of<NetId>(targetEntity))
					targetNetRaw = m_world.get<NetId>(targetEntity).Raw();
			}

			const fb::fbKinematicState kine(
				rs.kineState.startEpoch,
				rs.kineState.phase,
				rs.kineState.t,
				targetNetRaw,
				rs.kineState.eventMask,
				E2U(rs.kineType));

			return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base++, nullptr, nullptr, nullptr, nullptr, &kine);
		}

		PackedRigidDelta128 packed{};
		if (!PackRigidDelta128(bs->second.pos, bs->second.rot, rs, packed))
			return BuildFullActorEntity(e, userId);

		auto& userCache = m_cachedRigidDeltaPerUser[userId];
		if (auto it = userCache.find(netId); it != userCache.end())
		{
			if (it->second == packed)
				return 0;
		}

		userCache[netId] = packed;

		const fb::fbTransformDelta rigidDelta(packed.data0, packed.data1);
		uint32& base = userEntityBase[netId];

		{
			auto& newBs = userRigidBaseline[netId];
			newBs.pos = rs.pose.p;
			newBs.rot = rs.pose.q;
		}

		return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base++, nullptr, &rigidDelta);
	}

	void ServerReplicationSystem::QueueLifecycleCreateForUser(uint64 userId, NetId netId)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		auto& pending = m_pendingLifecyclePerUser[userId];
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

		auto& pending = m_pendingLifecyclePerUser[userId];
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

		auto& pending = m_pendingLifecyclePerUser[userId];
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
		auto userIt = m_pendingLifecyclePerUser.find(userId);
		if (userIt == m_pendingLifecyclePerUser.end())
			return;

		auto pendingIt = userIt->second.find(netId);
		if (pendingIt == userIt->second.end())
			return;

		if (pendingIt->second.op == fb::fbLifecycleOp_Remove && pendingIt->second.reason != fb::fbRemovalReason_Destroyed)
		{
			userIt->second.erase(pendingIt);
			if (userIt->second.empty())
				m_pendingLifecyclePerUser.erase(userIt);
		}
	}

	void ServerReplicationSystem::QueueLifecycleForVisibleActors(uint64 userId, const ServerAoiSystem* aoi, bool forceSyncUser)
	{
		if (userId == 0)
			return;

		if (aoi)
		{
			const UserAoiState* state = aoi->GetState(userId);
			if (!state)
				return;

			for (const NetId id : state->left)
				QueueRemovalForUser(userId, id, fb::fbRemovalReason_AoiLeft);

			for (const NetId id : state->entered)
			{
				CancelRemovalForUser(userId, id);

				if (IsActorKnownToUser(userId, id))
					QueueLifecycleMetaForUser(userId, id);
				else
					QueueLifecycleCreateForUser(userId, id);
			}

			for (const NetId id : state->visible)
			{
				if (!IsActorKnownToUser(userId, id))
				{
					QueueLifecycleCreateForUser(userId, id);
					continue;
				}

				if (forceSyncUser)
					QueueLifecycleMetaForUser(userId, id);
			}

			return;
		}

		auto view = m_world.view<NetId>();
		for (auto e : view)
		{
			const NetId netId = view.get<NetId>(e);
			if (!netId.IsValid())
				continue;

			if (!IsActorKnownToUser(userId, netId))
			{
				QueueLifecycleCreateForUser(userId, netId);
				continue;
			}

			if (forceSyncUser)
				QueueLifecycleMetaForUser(userId, netId);
		}
	}

	void ServerReplicationSystem::EmitPendingLifecyclePackets(ServerNetWorld& nw, uint64 userId, uint32 tick)
	{
		auto userIt = m_pendingLifecyclePerUser.find(userId);
		if (userIt == m_pendingLifecyclePerUser.end() || userIt->second.empty())
			return;

		std::vector<std::pair<NetId, PendingLifecycleEvent>> pending;
		pending.reserve(userIt->second.size());
		for (const auto& [netId, event] : userIt->second)
			pending.emplace_back(netId, event);

		auto estimateEventBytes = [](const PendingLifecycleEvent& event) -> size_t
		{
			switch (event.op)
			{
			case fb::fbLifecycleOp_Create: return 72;
			case fb::fbLifecycleOp_Meta: return 64;
			case fb::fbLifecycleOp_Remove: return 20;
			default: return 32;
			}
		};

		size_t cursor = 0;
		while (cursor < pending.size())
		{
			m_fbb->Clear();

			std::vector<flatbuffers::Offset<fb::fbLifecycleActor>> actorOffs;
			std::vector<std::pair<NetId, PendingLifecycleEvent>> sentEvents;
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
					break;

				entt::entity e = entt::null;
				if (event.op != fb::fbLifecycleOp_Remove)
				{
					auto netView = m_world.view<NetId>();
					for (auto candidate : netView)
					{
						if (netView.get<NetId>(candidate) == netId)
						{
							e = candidate;
							break;
						}
					}
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
			const auto batch = fb::CreatefbLifecycleBatch(*m_fbb, tick, vec);
			m_fbb->Finish(batch, fb::fbLifecycleBatchIdentifier());

			auto buf = PacketBuilder::CreateCustomPacket(
				CustomPacketId::LIFECYCLE,
				PacketFlags::NONE,
				eChannelType::RELIABLE_ORDERED,
				m_fbb->GetBufferPointer(),
				m_fbb->GetSize());

			if (!buf)
				continue;

			TransportInfo info{};
			info.method = eTransportMethod::Single;
			info.userId = userId;
			nw.Send(info, buf);

			CommitPendingLifecycleBatch(userId, sentEvents);
		}
	}

	void ServerReplicationSystem::CommitPendingLifecycleBatch(uint64 userId, const std::vector<std::pair<NetId, PendingLifecycleEvent>>& sentEvents)
	{
		auto userIt = m_pendingLifecyclePerUser.find(userId);
		if (userIt == m_pendingLifecyclePerUser.end())
			return;

		for (const auto& [netId, event] : sentEvents)
		{
			auto pendingIt = userIt->second.find(netId);
			if (pendingIt == userIt->second.end())
				continue;

			if (event.op == fb::fbLifecycleOp_Create)
			{
				MarkActorKnownToUser(userId, netId);
				m_forceFullStateBudgetPerUser[userId][netId] = kCreateFullStateBudget;

				auto view = m_world.view<NetId, OwnershipTag>();
				for (auto e : view)
				{
					if (view.get<NetId>(e) != netId)
						continue;

					if (view.get<OwnershipTag>(e).userId == userId && m_world.all_of<NetSpawnRequestId>(e))
						m_world.remove<NetSpawnRequestId>(e);
					break;
				}
			}
			else if (event.op == fb::fbLifecycleOp_Remove && event.reason == fb::fbRemovalReason_Destroyed)
			{
				ForgetActorForUser(userId, netId);
			}

			userIt->second.erase(pendingIt);
		}

		if (userIt->second.empty())
			m_pendingLifecyclePerUser.erase(userIt);
	}

	void ServerReplicationSystem::InvalidateUserCaches(uint64 userId, NetId netId)
	{
		if (auto it = m_entityBaselinePerUser.find(userId); it != m_entityBaselinePerUser.end())
		{
			it->second.erase(netId);
			if (it->second.empty())
				m_entityBaselinePerUser.erase(it);
		}

		if (auto it = m_rigidBaselineStatesPerUser.find(userId); it != m_rigidBaselineStatesPerUser.end())
		{
			it->second.erase(netId);
			if (it->second.empty())
				m_rigidBaselineStatesPerUser.erase(it);
		}

		if (auto it = m_characterBaselineStatesPerUser.find(userId); it != m_characterBaselineStatesPerUser.end())
		{
			it->second.erase(netId);
			if (it->second.empty())
				m_characterBaselineStatesPerUser.erase(it);
		}

		if (auto it = m_kineBaselineStatesPerUser.find(userId); it != m_kineBaselineStatesPerUser.end())
		{
			it->second.erase(netId);
			if (it->second.empty())
				m_kineBaselineStatesPerUser.erase(it);
		}

		if (auto it = m_cachedRigidDeltaPerUser.find(userId); it != m_cachedRigidDeltaPerUser.end())
		{
			it->second.erase(netId);
			if (it->second.empty())
				m_cachedRigidDeltaPerUser.erase(it);
		}

		if (auto it = m_cachedCharacterDeltaPerUser.find(userId); it != m_cachedCharacterDeltaPerUser.end())
		{
			it->second.erase(netId);
			if (it->second.empty())
				m_cachedCharacterDeltaPerUser.erase(it);
		}
	}

	void ServerReplicationSystem::InvalidateAllUserCaches(NetId netId)
	{
		for (auto it = m_entityBaselinePerUser.begin(); it != m_entityBaselinePerUser.end();)
		{
			it->second.erase(netId);
			if (it->second.empty()) it = m_entityBaselinePerUser.erase(it);
			else ++it;
		}

		for (auto it = m_rigidBaselineStatesPerUser.begin(); it != m_rigidBaselineStatesPerUser.end();)
		{
			it->second.erase(netId);
			if (it->second.empty()) it = m_rigidBaselineStatesPerUser.erase(it);
			else ++it;
		}

		for (auto it = m_characterBaselineStatesPerUser.begin(); it != m_characterBaselineStatesPerUser.end();)
		{
			it->second.erase(netId);
			if (it->second.empty()) it = m_characterBaselineStatesPerUser.erase(it);
			else ++it;
		}

		for (auto it = m_kineBaselineStatesPerUser.begin(); it != m_kineBaselineStatesPerUser.end();)
		{
			it->second.erase(netId);
			if (it->second.empty()) it = m_kineBaselineStatesPerUser.erase(it);
			else ++it;
		}

		for (auto it = m_cachedRigidDeltaPerUser.begin(); it != m_cachedRigidDeltaPerUser.end();)
		{
			it->second.erase(netId);
			if (it->second.empty()) it = m_cachedRigidDeltaPerUser.erase(it);
			else ++it;
		}

		for (auto it = m_cachedCharacterDeltaPerUser.begin(); it != m_cachedCharacterDeltaPerUser.end();)
		{
			it->second.erase(netId);
			if (it->second.empty()) it = m_cachedCharacterDeltaPerUser.erase(it);
			else ++it;
		}
	}

	bool ServerReplicationSystem::IsActorKnownToUser(uint64 userId, NetId netId) const
	{
		if (userId == 0 || !netId.IsValid())
			return false;

		return m_knownActorsPerUser.contains(ActorUserKey{ .userId = userId, .netId = netId.Raw() });
	}

	void ServerReplicationSystem::MarkActorKnownToUser(uint64 userId, NetId netId)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		m_knownActorsPerUser.insert(ActorUserKey{ .userId = userId, .netId = netId.Raw() });
	}

	void ServerReplicationSystem::ForgetActorForUser(uint64 userId, NetId netId)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		m_knownActorsPerUser.erase(ActorUserKey{ .userId = userId, .netId = netId.Raw() });
		if (auto it = m_forceFullStateBudgetPerUser.find(userId); it != m_forceFullStateBudgetPerUser.end())
		{
			it->second.erase(netId);
			if (it->second.empty())
				m_forceFullStateBudgetPerUser.erase(it);
		}
	}

	void ServerReplicationSystem::ForgetActorForAllUsers(NetId netId)
	{
		if (!netId.IsValid())
			return;

		const uint32 raw = netId.Raw();
		std::erase_if(m_knownActorsPerUser, [raw](const ActorUserKey& key) { return key.netId == raw; });
		for (auto it = m_forceFullStateBudgetPerUser.begin(); it != m_forceFullStateBudgetPerUser.end();)
		{
			it->second.erase(netId);
			if (it->second.empty()) it = m_forceFullStateBudgetPerUser.erase(it);
			else ++it;
		}
	}

	bool ServerReplicationSystem::ShouldForceFullState(uint64 userId, NetId netId) const
	{
		if (userId == 0 || !netId.IsValid())
			return false;

		if (auto it = m_forceFullStateBudgetPerUser.find(userId); it != m_forceFullStateBudgetPerUser.end())
		{
			if (auto jt = it->second.find(netId); jt != it->second.end())
				return jt->second > 0;
		}

		return false;
	}

	void ServerReplicationSystem::MarkFullStateSent(uint64 userId, NetId netId)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		auto it = m_forceFullStateBudgetPerUser.find(userId);
		if (it == m_forceFullStateBudgetPerUser.end())
			return;

		auto jt = it->second.find(netId);
		if (jt == it->second.end())
			return;

		if (jt->second > 0)
			--jt->second;

		if (jt->second == 0)
			it->second.erase(jt);

		if (it->second.empty())
			m_forceFullStateBudgetPerUser.erase(it);
	}
}
