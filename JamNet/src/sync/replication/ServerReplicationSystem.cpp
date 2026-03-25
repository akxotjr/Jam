#include "pch.h"
#include "jamnet/sync/replication/ServerReplicationSystem.h"

#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationUtils.h"
#include "jamnet/sync/replication/ServerInputSystem.h"

#include "jamnet/sync/networld/ServerNetWorld.h"
#include "jamnet/sync/replication/NetWorldContext.h"

#include "jamnet/sync/transport/CustomPacketHelper.h"

#include "jamnet/sync/schema/gen/snapshot_generated.h"

namespace jam::net
{

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

		// tick-local full cache reset
		if (m_fullCacheTick != m_tickCounter)
		{
			m_fullCacheTick = m_tickCounter;
			m_cachedRigidFull.clear();
			m_cachedCharacterFull.clear();
		}

		const bool periodicFull = ((m_tickCounter % kFullIntervalTicks) == 0);

		const uint32 tick = m_world.ctx().get<TickCounter>().tick;

		nw->FanOut([&](uint64 user) -> shared_ptr<SendBuffer>
			{
				const uint32 ack = m_world.ctx().get<ServerInputSystem>().LastProcessedSeq(user);

				bool forceFullMetaUser = false;
				bool forceFullMetaSent = false;

				auto it = m_forceFullMetaPerUsers.find(user);
				if (it != m_forceFullMetaPerUsers.end() && it->second > 0)
				{
					forceFullMetaUser = true;
				}

				constexpr size_t kBatch = 128;
				vector<flatbuffers::Offset<fb::fbActorEntity>> offs;
				offs.reserve(kBatch);

				auto view = m_world.view<NetId, NetActorBodyType>();

				m_fbb->Clear();
				
				for (auto e : view)
				{
					const NetId nid  = m_world.get<NetId>(e);
					const MetaSentKey key{ .userId = user, .netId = nid.Raw() };

					if (forceFullMetaUser)
					{
						EnsureMetaResendBudget(e, key);

						auto off = BuildFullActorEntity(e, user, true);
						if (!off.IsNull())
							offs.push_back(off);

						forceFullMetaSent = true;

						continue;
					}

					if (ShouldIncludeMeta(e, key))
					{
						auto off = BuildFullActorEntity(e, user, true);
						if (!off.IsNull())
							offs.push_back(off);

						continue;
					}

					//todo: active list 기반으로 되는지 체크
					//if (px::IsStaticBody(body))
					//	continue;

					if (periodicFull)
					{
						auto off = BuildFullActorEntity(e, user, false);
						if (!off.IsNull())
							offs.push_back(off);
					}
					else
					{
						auto off = BuildDeltaActorEntity(e, user);
						if (!off.IsNull())
							offs.push_back(off);
					}
				}

				if (offs.empty())
					return {};

				if (forceFullMetaSent)
				{
					if (it->second > 0)
						--it->second;
					if (it->second <= 0)
						m_forceFullMetaPerUsers.erase(it);
				}

				const auto header = fb::CreatefbSnapshotHeader(*m_fbb, tick, ack);
				const auto vec	  = m_fbb->CreateVector(offs);
				const auto snap	  = fb::CreatefbSnapshot(*m_fbb, header, vec);
				m_fbb->Finish(snap, fb::fbSnapshotIdentifier());

				return PacketBuilder::CreateCustomPacket(CustomPacketId::SNAPSHOT, PacketFlags::NONE, eChannelType::UNRELIABLE_SEQUENCED, m_fbb->GetBufferPointer(), m_fbb->GetSize());
			});
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

		std::erase_if(m_metaResendBudget, [userId](const auto& kv) { return kv.first.userId == userId; });
		std::erase_if(m_metaSent, [userId](const auto& k) { return k.userId == userId; });
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

			uint32 base = 0;
			if (auto it = userEntityBase.find(netId); it != userEntityBase.end())
				base = it->second;

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

			uint32 base = 0;
			if (auto it = userEntityBase.find(netId); it != userEntityBase.end())
				base = it->second;

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

		uint32 base = 0;
		if (auto it = userEntityBase.find(netId); it != userEntityBase.end())
			base = it->second;

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

	void ServerReplicationSystem::EnsureMetaResendBudget(entt::entity e, const MetaSentKey& key)
	{
		if (!m_world.all_of<NewlyCreatedTag>(e))
			return;

		if (m_metaResendBudget.contains(key))
			return;

		constexpr uint8 kDefaultBudget = 5;
		m_metaResendBudget.emplace(key, kDefaultBudget);
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

		// 1) spawn_req_id는 요청자(= owner)에게만 의미가 있으므로
		//    "요청자에게 충분히 보냈는가"만 보고 제거한다.
		if (done && m_world.all_of<NetSpawnRequestId>(e))
		{
			const uint64 owner = m_world.get<OwnershipTag>(e).userId;
			if (owner != 0 && owner == key.userId)
			{
				auto temp = m_world.get<NetSpawnRequestId>(e).requestId;
				m_world.remove<NetSpawnRequestId>(e);
				JAMNET_LOG_DEBUG("[OnMetaSent] userId= {}, remove NetSpawnRequestId= {}", owner, temp);
			}
		}

		// 2) NewlyCreatedTag는 "모든 유저에게 meta를 충분히 보냈는가" 관점으로 제거한다.
		//    - spawn_req_id는 요청자 전용이므로, 여기 판단에서 제외되어도 됨.
		//    - CanClearNewlyCreatedTag는 (metaSent/metaResendBudget) 기준으로 유저 전체를 검사한다.
		const NetId netId = m_world.get<NetId>(e);
		if (CanClearNewlyCreatedTag(netId))
		{
			m_world.remove<NewlyCreatedTag>(e);

			// 하위 호환/안전: 혹시 남아있으면 제거(요청자에게만 보내도록 마스킹했지만, 누수 방지)
			if (m_world.all_of<NetSpawnRequestId>(e))
				m_world.remove<NetSpawnRequestId>(e);
		}
	}

	bool ServerReplicationSystem::CanClearNewlyCreatedTag(NetId netId)
	{
		auto* nw = m_world.ctx().get<ServerNetWorld*>();
		if (!nw) return true;

		vector<uint64> users;
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
