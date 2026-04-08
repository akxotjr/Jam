#include "pch.h"
#include "jamnet/sync/replication/ServerReplicationSystem.h"

#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationUtils.h"
#include "jamnet/sync/replication/ServerInputSystem.h"

#include "jamnet/sync/networld/ServerNetWorld.h"
#include "jamnet/sync/replication/NetWorldContext.h"
#include "jamnet/sync/replication/ServerAoiSystem.h"

#include "jamnet/sync/transport/CustomPacketHelper.h"

#include "jamnet/sync/schema/gen/snapshot_generated.h"

namespace jam::net
{
	namespace
	{
		constexpr uint8 kAoiLeftRemovalBudget   = 5;
		constexpr uint8 kDestroyedRemovalBudget = 8;
		constexpr uint8 kMetaResendBudget       = 5;
		constexpr size_t kRemovedBatch          = 96;
		constexpr size_t kRemovedPayloadBytes   = 20;
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

		m_metaSent.clear();
		m_metaResendBudget.clear();
		m_pendingRemovalsPerUser.clear();

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

		// tick-local full cache reset
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
			const uint32 ack = m_world.ctx().get<ServerInputSystem>().LastProcessedSeq(user);

			std::unordered_set<NetId> sentThisTick;
			sentThisTick.reserve(256);

			bool forceFullMetaUser	   = false;
			bool forceFullMetaConsumed = false;

			auto it = m_forceFullMetaPerUsers.find(user);
			if (it != m_forceFullMetaPerUsers.end() && it->second > 0)
				forceFullMetaUser = true;

			std::unordered_set<uint32> enteredSet;
			if (aoi)
			{
				if (const UserAoiState* st = aoi->GetState(user))
				{
					enteredSet.reserve(st->entered.size());
					for (const NetId id : st->entered)
					{
						enteredSet.insert(id.Raw());
						CancelRemovalForUser(user, id);
					}

					for (const NetId id : st->left)
						QueueRemovalForUser(user, id, fb::fbRemovalReason_AoiLeft);
				}
			}

			EmitPendingRemovalSnapshots(*nw, user, tick, ack);

			std::array<std::vector<Candidate>, static_cast<size_t>(eBucket::Count)> buckets;
			auto view = m_world.view<NetId, NetActorBodyType>();

			// 1) candidate 분류
			for (auto e : view)
			{
				const NetId nid = m_world.get<NetId>(e);

				if (m_world.all_of<ReplicationStaticTag>(e))
					continue;

				// AOI 필터
				if (aoi && !aoi->IsVisible(user, nid))
					continue;

				const MetaSentKey key{ .userId = user, .netId = nid.Raw() };

				const bool includeMeta	   = forceFullMetaUser || ShouldIncludeMeta(e, key);
				const bool baselineInvalid = (!m_entityBaselinePerUser.contains(user)) || (!m_entityBaselinePerUser[user].contains(nid));
				const bool enteredNow	   = enteredSet.contains(nid.Raw());

				Candidate c{ .e = e, .netId = nid, .includeMeta = includeMeta };

				if (includeMeta || baselineInvalid || enteredNow)
				{
					c.useFull = true;
					buckets[static_cast<size_t>(eBucket::B0_MustSendFullMeta)].push_back(c);
					continue;
				}

				if (periodicFull)
				{
					c.useFull = true;
					buckets[static_cast<size_t>(eBucket::B1_MustSendFull)].push_back(c);
					continue;
				}

				if (!m_world.all_of<ReplicationActiveTag>(e))
				{
					buckets[static_cast<size_t>(eBucket::B4_LowPriority)].push_back(c);
					continue;
				}

				const auto body = m_world.get<NetActorBodyType>(e).body;
				if (body == px::eBodyType::Character)
					buckets[static_cast<size_t>(eBucket::B2_HighDelta)].push_back(c);
				else
					buckets[static_cast<size_t>(eBucket::B3_NormalDelta)].push_back(c);
			}

			// 2) 버킷 순서대로 전송 큐 구성
			std::vector<Candidate> ordered;
			ordered.reserve(
				buckets[0].size() + buckets[1].size() + buckets[2].size() +
				buckets[3].size() + buckets[4].size());

			auto appendAll = [&](eBucket b)
				{
					auto& src = buckets[static_cast<size_t>(b)];
					ordered.insert(ordered.end(), src.begin(), src.end());
				};

			appendAll(eBucket::B0_MustSendFullMeta);
			appendAll(eBucket::B1_MustSendFull);
			appendAll(eBucket::B2_HighDelta);
			appendAll(eBucket::B3_NormalDelta);

			//// LowPriority는 샘플링
			//if ((m_tickCounter % 4) == 0)
			//{
			//	auto& low = buckets[static_cast<size_t>(eBucket::B4_LowPriority)];
			//	const size_t lowCap = std::min<size_t>(8, low.size());
			//	ordered.insert(ordered.end(), low.begin(), low.begin() + lowCap);
			//}

			if (ordered.empty())
				continue;

			constexpr size_t kBatch = 128;
			constexpr size_t kSnapshotPayloadBudget = JAMNET_MTU - PacketHeader::HALF_SIZE - 24;

			auto estimateActorBytes = [&](const Candidate& c) -> size_t
				{
					size_t bytes = 28; // fbActorEntity 기본 오버헤드
					if (c.includeMeta) bytes += 40;

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

			// 3) same-tick multi packet 생성/송신
			size_t cursor = 0;
			while (cursor < ordered.size())
			{
				m_fbb->Clear();

				std::vector<flatbuffers::Offset<fb::fbActorEntity>> offs;
				offs.reserve(kBatch);

				size_t usedPayloadBudget = 0;
				const size_t beginCursor = cursor;

				for (; cursor < ordered.size(); ++cursor)
				{
					const Candidate& c = ordered[cursor];

					if (offs.size() >= kBatch)
						break;

					const size_t est = estimateActorBytes(c);

					if (est > kSnapshotPayloadBudget)
					{
						JAMNET_LOG_WARN("[Snapshot] single actor too large. user={}, netId={}", user, c.netId.Raw());
						continue;
					}

					if ((usedPayloadBudget + est) > kSnapshotPayloadBudget)
					{
						// 현재 packet은 확정, 다음 packet에서 재시도
						if (!offs.empty())
							break;
						// 첫 아이템부터 초과면 스킵(무한루프 방지)
						continue;
					}

					flatbuffers::Offset<fb::fbActorEntity> off = 0;
					if (c.useFull)
						off = BuildFullActorEntity(c.e, user, c.includeMeta);
					else
						off = BuildDeltaActorEntity(c.e, user);

					if (off.IsNull())
						continue;

					if (sentThisTick.contains(c.netId))
						continue;

					sentThisTick.insert(c.netId);

					offs.push_back(off);
					usedPayloadBudget += est;

					if (forceFullMetaUser && c.includeMeta)
						forceFullMetaConsumed = true;
				}

				// 진행이 없으면 종료
				if (offs.empty())
				{
					if (cursor == beginCursor)
						++cursor;
					continue;
				}

				const auto header = fb::CreatefbSnapshotHeader(*m_fbb, tick, ack);
				const auto vec    = m_fbb->CreateVector(offs);
				const auto snap   = fb::CreatefbSnapshot(*m_fbb, header, vec, 0);
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

			// force full meta budget 차감
			if (forceFullMetaUser && forceFullMetaConsumed && it != m_forceFullMetaPerUsers.end())
			{
				if (it->second > 0)
					--it->second;
				if (it->second <= 0)
					m_forceFullMetaPerUsers.erase(it);
			}
		}
	}

	/// @note	The premise that called only on the thread of ServerNetWorld's ShardExcutor
	void ServerReplicationSystem::ForceFullMetaForUser(uint64 userId, int32 budget)
	{
		if (userId == 0 || budget <= 0)
			return;

		auto& slot = m_forceFullMetaPerUsers[userId];
		slot = std::max(slot, budget);
	}

	void ServerReplicationSystem::OnEnter(uint64 userId)
	{
		if (userId == 0) return;

		JAMNET_LOG_DEBUG("[ServerReplicationSystem] OnEnter() : userId= {}", userId);

		ForceFullMetaForUser(userId, 5);
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

		m_forceFullMetaPerUsers.erase(userId);
		m_pendingRemovalsPerUser.erase(userId);

		std::erase_if(m_metaResendBudget, [userId](const auto& kv) { return kv.first.userId == userId; });
		std::erase_if(m_metaSent, [userId](const auto& k) { return k.userId == userId; });
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
		flatbuffers::Offset<fb::fbActorMeta> meta = 0;

		uint64 owner		= 0;
		uint64 controller	= 0;
		uint64 prefab		= 0;
		uint32 spawnReqId	= 0;
		uint32 packedId		= 0;

		if (auto* o	= m_world.try_get<OwnershipTag>(e))			 owner		= o->userId;
		if (auto* c	= m_world.try_get<ControlTag>(e))			 controller = c->userId;
		if (auto* p	= m_world.try_get<NetPrefabKey>(e))			 prefab		= p->key.value;
		if (auto* tpr = m_world.try_get<NetTeamPartRole>(e))	 packedId	= tpr->Packed();

		// spawn_req_id는 "요청자(client local)" 범위 식별자다.
		// 따라서 요청자(현재는 owner로 가정)에게만 내려준다.
		if (userId != 0 && userId == owner)
		{
			if (auto* s = m_world.try_get<NetSpawnRequestId>(e))
				spawnReqId = s->requestId;
		}

		meta = fb::CreatefbActorMeta(*m_fbb, owner, controller, prefab, spawnReqId, packedId);
		
		return meta;
	}

	flatbuffers::Offset<fb::fbActorEntity> ServerReplicationSystem::BuildFullActorEntity(entt::entity e, uint64 userId, bool includeMeta)
	{
		const NetId  netId    = m_world.get<NetId>(e);
		const auto	 bodyType = m_world.get<NetActorBodyType>(e).body;

		auto& userEntityBase = m_entityBaselinePerUser[userId];

		if (bodyType == px::eBodyType::Character)
		{
			auto& cs = m_world.get<px::CharacterState>(e);

			const float facingYaw		= cs.facingYaw;
			const float facingPitch		= cs.facingPitch;

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

			// per-user baseline 갱신
			{
				auto& bs = m_characterBaselineStatesPerUser[userId][netId];
				bs.pos   = cs.pos;
				bs.yaw	 = facingYaw;
				bs.pitch = facingPitch;
			}

			const fb::fbCharacterFull160 charFull(packed.data0, packed.data1, packed.data2);

			uint32& base = userEntityBase.try_emplace(netId, 0u).first->second;

			flatbuffers::Offset<fb::fbActorMeta> meta = 0;
			if (includeMeta) meta = BuildActorMeta(e, userId);

			auto off = fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base, meta, nullptr, nullptr, &charFull, nullptr);
			if (!off.IsNull() && includeMeta)
			{
				const MetaSentKey key{ .userId = userId, .netId = netId.Raw() };
				OnMetaSent(e, key);
			}
			return off;
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
				{
					targetNetRaw = m_world.get<NetId>(targetEntity).Raw();
				}
			}

			const fb::fbKinematicState kine(
				rs.kineState.startEpoch,
				rs.kineState.phase,
				rs.kineState.t,
				targetNetRaw,
				rs.kineState.eventMask,
				static_cast<uint8>(rs.kineType)
			);

			uint32& base = userEntityBase.try_emplace(netId, 0u).first->second;

			flatbuffers::Offset<fb::fbActorMeta> meta = 0;
			if (includeMeta) meta = BuildActorMeta(e, userId);

			auto off = fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base, meta, nullptr, nullptr, nullptr, nullptr, &kine);
			if (!off.IsNull() && includeMeta)
			{
				const MetaSentKey key{ .userId = userId, .netId = netId.Raw() };
				OnMetaSent(e, key);
			}
			return off;
		}

		PackedRigidFull192 packed{};

		if (auto it = m_cachedRigidFull.find(netId); it != m_cachedRigidFull.end())
		{
			packed = it->second;
		}
		else
		{
			if (!PackRigidFull192(rs, packed)) return 0;
			m_cachedRigidFull.emplace(netId, packed);
		}

		{
			auto& bs = m_rigidBaselineStatesPerUser[userId][netId];
			bs.pos = rs.pose.p;
			bs.rot = rs.pose.q;
		}

		fb::fbTransformFull rigidFull(packed.data0, packed.data1, packed.data2);

		uint32& base = userEntityBase.try_emplace(netId, 0u).first->second;

		flatbuffers::Offset<fb::fbActorMeta> meta = 0;
		if (includeMeta) meta = BuildActorMeta(e, userId);

		auto off = fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base, meta, &rigidFull);
		if (!off.IsNull() && includeMeta)
		{
			const MetaSentKey key{ .userId = userId, .netId = netId.Raw() };
			OnMetaSent(e, key);
		}
		return off;
	}

	flatbuffers::Offset<fb::fbActorEntity> ServerReplicationSystem::BuildDeltaActorEntity(entt::entity e, uint64 userId)
	{
		const NetId  netId	  = m_world.get<NetId>(e);
		const auto	 bodyType = m_world.get<NetActorBodyType>(e).body;

		auto& userEntityBase = m_entityBaselinePerUser[userId];

		if (bodyType == px::eBodyType::Character)
		{
			auto& cs = m_world.get<px::CharacterState>(e);

			auto& userCharBaseline = m_characterBaselineStatesPerUser[userId];
			auto bs = userCharBaseline.find(netId);
			if (bs == userCharBaseline.end())
				return BuildFullActorEntity(e, userId, false);

			PackedCharacterDelta128 packedDelta{};
			if (!PackCharacterDelta128(bs->second.pos, bs->second.yaw, bs->second.pitch, cs, packedDelta))
			{
				return BuildFullActorEntity(e, userId, false);
			}

			auto& userCache = m_cachedCharacterDeltaPerUser[userId];
			if (auto it = userCache.find(netId); it != userCache.end())
			{
				if (it->second.data0 == packedDelta.data0 && it->second.data1 == packedDelta.data1)
					return 0; // unchanged for this user
			}

			userCache[netId] = packedDelta;

			const fb::fbCharacterDelta128 charDelta(packedDelta.data0, packedDelta.data1);

			uint32& base = userEntityBase[netId];

			{
				auto& newBs = userCharBaseline[netId];
				newBs.pos	= cs.pos;
				newBs.yaw	= cs.facingYaw;
				newBs.pitch = cs.facingPitch;
			}

			return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base++, 0, nullptr, nullptr, nullptr, &charDelta);
		}

		// rigid delta
		auto& userRigidBaseline = m_rigidBaselineStatesPerUser[userId];
		auto bs = userRigidBaseline.find(netId);
		if (bs == userRigidBaseline.end())
			return BuildFullActorEntity(e, userId, false);

		auto& rs = m_world.get<px::RigidState>(e);

		if (rs.kineType != px::eKineDrivenType::RuntimeDynamic)
		{
			if (rs.kineState.startEpoch == 0)
			{
				rs.kineState.startEpoch = m_world.ctx().get<TickCounter>().tick;
			}

			auto& userKineBaseline = m_kineBaselineStatesPerUser[userId];
			auto itBs = userKineBaseline.find(netId);
			if (itBs == userKineBaseline.end())
				return BuildFullActorEntity(e, userId, false);

			if (itBs->second == rs.kineState)
				return 0; // unchanged

			itBs->second = rs.kineState;

			uint32& base = userEntityBase[netId];

			uint32 targetNetRaw = NetId::Invalid().Raw();
			if (rs.kineState.targetId != px::INVALID_OBJ_ID)
			{
				const entt::entity targetEntity = static_cast<entt::entity>(rs.kineState.targetId);
				if (m_world.valid(targetEntity) && m_world.all_of<NetId>(targetEntity))
				{
					targetNetRaw = m_world.get<NetId>(targetEntity).Raw();
				}
			}

			const fb::fbKinematicState kine(
				rs.kineState.startEpoch,
				rs.kineState.phase,
				rs.kineState.t,
				targetNetRaw,
				rs.kineState.eventMask,
				E2U(rs.kineType)
			);

			return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base++, 0, nullptr, nullptr, nullptr, nullptr, &kine);
		}

		PackedRigidDelta128 packed{};
		if (!PackRigidDelta128(bs->second.pos, bs->second.rot, rs, packed))
			return BuildFullActorEntity(e, userId, false);

		auto& userCache = m_cachedRigidDeltaPerUser[userId];
		if (auto it = userCache.find(netId); it != userCache.end())
		{
			if (it->second == packed)
				return 0; // unchanged for this user
		}

		userCache[netId] = packed;

		const fb::fbTransformDelta rigidDelta(packed.data0, packed.data1);

		uint32& base = userEntityBase[netId];

		// baseline 갱신
		{
			auto& newBs = userRigidBaseline[netId];
			newBs.pos = rs.pose.p;
			newBs.rot = rs.pose.q;
		}

		return fb::CreatefbActorEntity(*m_fbb, netId.Raw(), base++, 0, nullptr, &rigidDelta);
	}

	void ServerReplicationSystem::QueueRemovalForUser(uint64 userId, NetId netId, fb::fbRemovalReason reason)
	{
		if (userId == 0 || !netId.IsValid())
			return;

		auto& removal = m_pendingRemovalsPerUser[userId][netId];
		const bool upgradeToDestroyed = (reason == fb::fbRemovalReason_Destroyed) || (removal.reason == fb::fbRemovalReason_Destroyed);

		removal.reason		 = upgradeToDestroyed ? fb::fbRemovalReason_Destroyed : reason;
		removal.resendBudget = std::max<uint8>(removal.resendBudget, upgradeToDestroyed ? kDestroyedRemovalBudget : kAoiLeftRemovalBudget);

		InvalidateUserCaches(userId, netId, removal.reason);
	}

	void ServerReplicationSystem::CancelRemovalForUser(uint64 userId, NetId netId)
	{
		auto userIt = m_pendingRemovalsPerUser.find(userId);
		if (userIt == m_pendingRemovalsPerUser.end())
			return;

		auto remIt = userIt->second.find(netId);
		if (remIt == userIt->second.end())
			return;

		if (remIt->second.reason != fb::fbRemovalReason_Destroyed)
		{
			userIt->second.erase(remIt);
			if (userIt->second.empty())
				m_pendingRemovalsPerUser.erase(userIt);
		}
	}

	void ServerReplicationSystem::EmitPendingRemovalSnapshots(ServerNetWorld& nw, uint64 userId, uint32 tick, uint32 ack)
	{
		auto userIt = m_pendingRemovalsPerUser.find(userId);
		if (userIt == m_pendingRemovalsPerUser.end() || userIt->second.empty())
			return;

		std::vector<std::pair<NetId, PendingRemoval>> pending;
		pending.reserve(userIt->second.size());
		for (const auto& [netId, removal] : userIt->second)
			pending.emplace_back(netId, removal);

		constexpr size_t kSnapshotPayloadBudget = JAMNET_MTU - PacketHeader::HALF_SIZE - 24;

		size_t cursor = 0;
		while (cursor < pending.size())
		{
			m_fbb->Clear();

			std::vector<flatbuffers::Offset<fb::fbRemovedActor>> removedOffs;
			std::vector<NetId> sentIds;
			removedOffs.reserve(kRemovedBatch);
			sentIds.reserve(kRemovedBatch);

			size_t usedPayloadBudget = 0;
			for (; cursor < pending.size(); ++cursor)
			{
				if (removedOffs.size() >= kRemovedBatch)
					break;

				if ((usedPayloadBudget + kRemovedPayloadBytes) > kSnapshotPayloadBudget)
					break;

				const auto& [netId, removal] = pending[cursor];
				removedOffs.push_back(
					fb::CreatefbRemovedActor(*m_fbb, netId.Raw(), removal.reason));
				sentIds.push_back(netId);
				usedPayloadBudget += kRemovedPayloadBytes;
			}

			if (removedOffs.empty())
				break;

			const auto header     = fb::CreatefbSnapshotHeader(*m_fbb, tick, ack);
			const auto removedVec = m_fbb->CreateVector(removedOffs);
			const auto snap       = fb::CreatefbSnapshot(*m_fbb, header, 0, removedVec);
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
			info.userId = userId;
			nw.Send(info, buf);

			CommitPendingRemovalBatch(userId, sentIds);
		}
	}

	void ServerReplicationSystem::CommitPendingRemovalBatch(uint64 userId, const std::vector<NetId>& sentIds)
	{
		auto userIt = m_pendingRemovalsPerUser.find(userId);
		if (userIt == m_pendingRemovalsPerUser.end())
			return;

		for (const NetId netId : sentIds)
		{
			auto remIt = userIt->second.find(netId);
			if (remIt == userIt->second.end())
				continue;

			if (remIt->second.resendBudget > 0)
				--remIt->second.resendBudget;

			if (remIt->second.resendBudget == 0)
				userIt->second.erase(remIt);
		}

		if (userIt->second.empty())
			m_pendingRemovalsPerUser.erase(userIt);
	}

	void ServerReplicationSystem::InvalidateUserCaches(uint64 userId, NetId netId, fb::fbRemovalReason reason)
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

		const MetaSentKey key{ .userId = userId, .netId = netId.Raw() };
		if (reason == fb::fbRemovalReason_Destroyed)
		{
			m_metaSent.erase(key);
			m_metaResendBudget.erase(key);
		}
		else
		{
			m_metaResendBudget[key] = std::max<uint8>(m_metaResendBudget[key], kMetaResendBudget);
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

		const uint32 raw = netId.Raw();
		std::erase_if(m_metaSent, [raw](const MetaSentKey& key) { return key.netId == raw; });
		std::erase_if(m_metaResendBudget, [raw](const auto& kv) { return kv.first.netId == raw; });
	}

	void ServerReplicationSystem::EnsureMetaResendBudget(entt::entity e, const MetaSentKey& key)
	{
		if (!m_world.all_of<NewlyCreatedTag>(e))
			return;

		if (m_metaResendBudget.contains(key))
			return;

		m_metaResendBudget.emplace(key, kMetaResendBudget);
	}

	bool ServerReplicationSystem::ShouldIncludeMeta(entt::entity e, const MetaSentKey& key)
	{
		EnsureMetaResendBudget(e, key);

		if (auto it = m_metaResendBudget.find(key); it != m_metaResendBudget.end())
		{
			if (it->second > 0)
				return true;
		}

		return !m_metaSent.contains(key);
	}

	void ServerReplicationSystem::OnMetaSent(entt::entity e, const MetaSentKey& key)
	{
		m_metaSent.insert(key);

		bool done = false;
		if (auto it = m_metaResendBudget.find(key); it != m_metaResendBudget.end())
		{
			if (it->second > 0)
				--it->second;

			if (it->second == 0)
			{
				m_metaResendBudget.erase(it);
				done = true;
			}
		}

		if (!m_world.valid(e) || !m_world.all_of<NewlyCreatedTag>(e))
			return;

		if (done && m_world.all_of<NetSpawnRequestId>(e))
		{
			const uint64 owner = m_world.get<OwnershipTag>(e).userId;
			if (owner != 0 && owner == key.userId)
				m_world.remove<NetSpawnRequestId>(e);
		}

		const NetId netId = m_world.get<NetId>(e);
		if (CanClearNewlyCreatedTag(netId))
		{
			m_world.remove<NewlyCreatedTag>(e);

			if (m_world.all_of<NetSpawnRequestId>(e))
				m_world.remove<NetSpawnRequestId>(e);
		}
	}

	bool ServerReplicationSystem::CanClearNewlyCreatedTag(NetId netId)
	{
		auto* nw = m_world.ctx().get<ServerNetWorld*>();
		if (!nw) return true;

		std::vector<uint64> users;
		nw->GetMembers(users);

		if (users.empty())
			return true;

		for (uint64 userId : users)
		{
			const MetaSentKey key{ .userId = userId, .netId = netId.Raw() };

			// 아직 resend budget 남아있으면 meta 전송이 더 필요하다고 판단
			if (auto it = m_metaResendBudget.find(key); it != m_metaResendBudget.end())
			{
				if (it->second > 0)
					return false;
			}

			// meta 자체를 한번도 안 보냈으면 clear 불가
			if (!m_metaSent.contains(key))
				return false;
		}

		return true;
	}
}
