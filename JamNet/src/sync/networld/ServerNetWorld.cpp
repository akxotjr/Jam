#include "pch.h"
#include "jamnet/sync/networld/ServerNetWorld.h"

#include "jamnet/sync/transport/ITransportEndpoint.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"

#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/NetWorldContext.h"
#include "jamnet/sync/replication/ServerInputSystem.h"
#include "jamnet/sync/replication/ServerPhysicsSystem.h"
#include "jamnet/sync/replication/ServerAoiSystem.h"
#include "jamnet/sync/replication/ServerReplicationSystem.h"

#include "jamnet/sync/schema/gen/actor_spawn_generated.h"
#include "jamnet/sync/schema/gen/input_generated.h"

namespace jam::net
{

	namespace
	{
		
		px::ObjectId BindingTarget(entt::registry& world, const entt::entity e, NetId target)
		{
			if (!target.IsValid()) return px::INVALID_OBJ_ID;

			px::ObjectId resolved = px::INVALID_OBJ_ID;
			auto view = world.view<NetId, PhysicsSpawnedTag>();

			for (auto candidate : view)
			{
				if (view.get<NetId>(candidate) == target)
				{
					resolved = MakeObjectId(candidate);
					world.emplace<TargetInfo>(e, TargetInfo{ .targetNetId = target, .targetObjId = resolved });

					return resolved;
				}
			}

			return px::INVALID_OBJ_ID;
		}


	} // anonymous namespace






	void ServerNetWorld::Init()
	{
		NetWorld::Init();

		if (!m_transport || !m_physics) return;

		m_physics->SetJobBridge(m_bridge.get());
		m_physics->Init();

		m_world.ctx().emplace<ServerNetWorld*>(this);
		m_world.ctx().emplace<TickCounter>().Init();
		m_world.ctx().emplace<ServerInputSystem>(m_world).Init();
		m_world.ctx().emplace<ServerPhysicsSystem>(m_world, m_physics.get()).Init();
		m_world.ctx().emplace<ServerAoiSystem>(m_world, m_physics.get()).Init();
		m_world.ctx().emplace<ServerReplicationSystem>(m_world).Init();

		BootstrapLevelActors();
	}

	void ServerNetWorld::SetTransportAdapter(ITransportEndpoint* transport)
	{
		m_transport = transport;
	}

	void ServerNetWorld::SetPhysicsFacade(std::unique_ptr<px::IPhysicsFacade> physics)
	{
		m_physics = std::move(physics);
	}

	bool ServerNetWorld::DespawnActorImmediate(NetId netId, uint64 userId)
	{
		return DespawnActorImpl(netId, userId);
	}



	void ServerNetWorld::Enter(uint64 userId)
	{
		if (userId == 0)
			return;

		Post(Job([this, userId]()
			{
				if (std::ranges::find(m_members, userId) == m_members.end())
					m_members.push_back(userId);

				if (auto* aoi = m_world.ctx().find<ServerAoiSystem>())
					aoi->OnEnter(userId);

				if (auto* repl = m_world.ctx().find<ServerReplicationSystem>())
					repl->OnEnter(userId);
			}));
	}

	void ServerNetWorld::Leave(uint64 userId)
	{
		if (userId == 0)
			return;

		Post(Job([this, userId]()
			{
				std::erase(m_members, userId);

				if (auto* aoi = m_world.ctx().find<ServerAoiSystem>())
					aoi->OnLeave(userId);

				if (auto* repl = m_world.ctx().find<ServerReplicationSystem>())
					repl->OnLeave(userId);
			}));
	}

	void ServerNetWorld::Send(const TransportInfo& info, const std::shared_ptr<SendBuffer>& buf)
	{
		if (m_transport)
			m_transport->Send(info, buf);
	}

	void ServerNetWorld::Multicast(const std::shared_ptr<SendBuffer>& buf)
	{
		TransportInfo info{};
		info.method = eTransportMethod::Multicast;
		info.groupId = GetGroupId();

		if (m_transport)
			m_transport->Send(info, buf);
	}

	void ServerNetWorld::FanOut(TransportInfo::PayloadFactory factory)
	{
		TransportInfo info{};
		info.method			= eTransportMethod::FanOut;
		info.groupId		= GetGroupId();
		info.payloadFactory = std::move(factory);

		if (m_transport)
			m_transport->Send(info, nullptr);
	}


	void ServerNetWorld::OnRecvPacket(const PacketView& pkt)
	{
		switch (pkt.Id())
		{
		case CustomPacketId::INPUT:
		{
			ProcessGameInput(pkt);
			break;
		}

		default: break;
		}
	}


	void ServerNetWorld::SpawnActor(SpawnParams params)
	{
		Post(Job([this, params = std::move(params)]()
			{
				SpawnActorImpl(std::move(params));
			}));
	}

	void ServerNetWorld::DespawnActor(const NetId netId, const uint64 userId)
	{
		Post(Job([this, netId, userId]()
			{
				DespawnActorImpl(netId, userId);
			}));
	}

	void ServerNetWorld::PossessActor(NetId netId, uint64 userId)
	{
		Post(Job([this, netId, userId]
			{
				PossessActorImpl(netId, userId);
			}));
	}

	void ServerNetWorld::UnpossessActor(NetId netId, uint64 userId)
	{
		Post(Job([this, netId, userId]
			{
				UnpossessActorImpl(netId, userId);
			}));
	}

	void ServerNetWorld::SpawnActorAsync(SpawnParams params, std::function<void(NetId)> onDone)
	{
		Post(Job([this, params = params, onDone = std::move(onDone)]()
			{
				//const NetId nid = SpawnActorImpl(params);
				//if (onDone) onDone(nid);

				NetId nid = NetId::Invalid();

				try
				{
					nid = SpawnActorImpl(std::move(params));
				}
				catch (...)
				{
					JAMNET_LOG_ERROR_LOC("[ServerNetWorld::SpawnActorAsync] SpawnActorImpl threw exception");
					nid = NetId::Invalid();
				}

				if (onDone) onDone(nid);
			}));
	}

	void ServerNetWorld::DespawnActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone)
	{
		Post(Job([this, netId, userId, onDone = std::move(onDone)]()
			{
				const bool ok = DespawnActorImpl(netId, userId);
				if (onDone) onDone(ok);
			}));
	}

	void ServerNetWorld::PossessActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone)
	{
		Post(Job([this, netId, userId, onDone = std::move(onDone)]()
			{
				const bool ok = PossessActorImpl(netId, userId);
				if (onDone) onDone(ok);
			}));
	}

	void ServerNetWorld::UnpossessActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone)
	{
		Post(Job([this, netId, userId, onDone = std::move(onDone)]()
			{
				const bool ok = UnpossessActorImpl(netId, userId);
				if (onDone) onDone(ok);
			}));
	}

	void ServerNetWorld::RequestInitialSnapshotNewClient()
	{
		m_pendingInitialFullSnapshot.store(true, std::memory_order_relaxed);
	}



	void ServerNetWorld::TickOnShard()
	{
		if (!m_world.ctx().contains<TickCounter>() 
			|| !m_world.ctx().contains<ServerInputSystem>() 
			|| !m_world.ctx().contains<ServerPhysicsSystem>()
			|| !m_world.ctx().contains<ServerAoiSystem>()
			|| !m_world.ctx().contains<ServerReplicationSystem>())
			return;

		m_world.ctx().get<TickCounter>().Tick();
		m_world.ctx().get<ServerInputSystem>().Tick();
		m_world.ctx().get<ServerPhysicsSystem>().Tick();
		m_world.ctx().get<ServerAoiSystem>().Tick();
		m_world.ctx().get<ServerReplicationSystem>().Tick();
	}

	void ServerNetWorld::BootstrapLevelActors()
	{
		if (!m_physics || m_levelPath.empty()) 
			return;

		m_levelLayerInfo = m_physics->SetLevelPath(m_levelPath);
		if (m_levelLayerInfo.totalCount == 0) 
			return;

		for (const auto& [layer, count] : m_levelLayerInfo.countPerLayer)
		{
			if (count == 0) continue;

			std::vector<px::LevelInstanceInfo> instances(count);
			std::vector<entt::entity> created;
			created.reserve(count);

			for (uint32 i = 0; i < count; ++i)
			{
				const entt::entity e = m_world.create();
				created.push_back(e);

				instances[i].objectId = MakeObjectId(e);
			}

			if (!m_physics->LoadLevel(layer, instances))
			{
				for (auto e : created)
					if (m_world.valid(e)) m_world.destroy(e);
				continue;
			}

			for (const auto& inst : instances)
			{
				const entt::entity e = static_cast<entt::entity>(inst.objectId);
				if (!m_world.valid(e)) continue;

				const NetId nid = NetId::MakeLevel(inst.levelActorId);
				if (!nid.IsValid()) continue;

				m_world.emplace_or_replace<NetId>(e, nid);
				m_world.emplace_or_replace<PhysicsSpawnedTag>(e);
				m_world.emplace_or_replace<NetActorBodyType>(e, NetActorBodyType{ .body = px::eBodyType::Rigid });
				m_world.emplace_or_replace<NetPrefabKey>(e, NetPrefabKey{ inst.prefab });
				m_world.emplace_or_replace<px::RigidState>(e, inst.state);
				m_world.emplace_or_replace<NewlyCreatedTag>(e);

				if (m_physics->GetMotionType(inst.objectId) == px::eMotionType::Static)
					m_world.emplace<ReplicationStaticTag>(e);
			}
		}
	}

	NetId ServerNetWorld::SpawnActorImpl(SpawnParams params)
	{
		if (!params.desc.prefab.IsValid())
			return NetId::Invalid();

		const entt::entity e = m_world.create();

		const NetId nid = NetId::MakeRuntime(m_netIdGenerator.fetch_add(1, std::memory_order_relaxed));

		m_world.emplace<NetId>(e, nid);
		m_world.emplace<NetPrefabKey>(e, NetPrefabKey{ params.desc.prefab });
		m_world.emplace<NewlyCreatedTag>(e);
		m_world.emplace<NetSpawnRequestId>(e, NetSpawnRequestId{ params.spawnId });
		m_world.emplace<OwnershipTag>(e, OwnershipTag{params.owner});
		m_world.emplace<ControlTag>(e, ControlTag{ params.controller });
		m_world.emplace<NetTeamPartRole>(e, NetTeamPartRole{
			.team = params.desc.team,
			.part = params.desc.part,
			.role = params.desc.role
		});

		params.desc.targetId = BindingTarget(m_world, e, params.targetNetId);

		m_world.ctx().get<ServerPhysicsSystem>().SpawnActor(e, params.desc);

		JAMNET_LOG_DEBUG("[ServerNetWorld::SpawnActorImpl] netId = {}, owner = {}, reqId = {}", nid.Raw(), params.owner, params.spawnId);

		return nid;
	}


	bool ServerNetWorld::DespawnActorImpl(NetId netId, uint64 userId)
	{
		// 1. NetId로 엔티티 찾기
		auto view = m_world.view<NetId>();
		entt::entity targetEntity = entt::null;

		for (auto actor : view)
		{
			if (view.get<NetId>(actor) == netId)
			{
				targetEntity = actor;
				break;
			}
		}

		if (targetEntity == entt::null)
			return false;

		if (auto* ownership = m_world.try_get<OwnershipTag>(targetEntity))
		{
			if (userId != 0 && ownership->userId != userId)
				return false;
		}

		if (auto* repl = m_world.ctx().find<ServerReplicationSystem>())
			repl->OnActorDestroyed(targetEntity);

		if (m_world.all_of<PhysicsSpawnedTag>(targetEntity))
		{
			if (auto* phys = m_world.ctx().find<ServerPhysicsSystem>())
			{
				phys->DespawnActor(targetEntity);
			}
			else
			{
				return false;
			}
		}

		m_world.destroy(targetEntity);

		return true;
	}

	bool ServerNetWorld::PossessActorImpl(NetId netId, uint64 userId)
	{
		auto view = m_world.view<NetId, OwnershipTag>();
		entt::entity target = entt::null;

		for (auto actor : view)
		{
			if (view.get<NetId>(actor) == netId)
			{
				target = actor;
				break;
			}
		}

		if (target == entt::null)
			return false;

		auto& ownership = view.get<OwnershipTag>(target);
		if (ownership.userId != userId)
			return false;

		m_world.emplace_or_replace<ControlTag>(target, ControlTag{ userId });
		m_world.emplace_or_replace<px::CharacterInput>(target);

		return true;
	}

	bool ServerNetWorld::UnpossessActorImpl(NetId netId, uint64 userId)
	{
		auto view = m_world.view<NetId, ControlTag>();
		entt::entity targetEntity = entt::null;

		for (auto actor : view)
		{
			if (view.get<NetId>(actor) == netId)
			{
				targetEntity = actor;
				break;
			}
		}

		if (targetEntity == entt::null)
			return false;

		auto& controllable = view.get<ControlTag>(targetEntity);

		// 권한 검증
		if (controllable.userId != userId)
			return false;

		// 조종 해제
		controllable.userId = 0;

		return true;
	}

	void ServerNetWorld::ProcessGameInput(const PacketView& pkt)
	{
		flatbuffers::Verifier verfier(pkt.Payload(), pkt.PayloadSize());
		if (!fb::VerifyfbGameInputBuffer(verfier)) return;

		const auto* gameInput = fb::GetfbGameInput(pkt.Payload());
		if (!gameInput) return;

		InputCmd cmd{};
		cmd.seq					= gameInput->sequence();
		cmd.input.inputFlags	= gameInput->flags();
		cmd.input.facingYaw		= gameInput->yaw();
		cmd.input.facingPitch	= gameInput->pitch();

		if (m_world.ctx().contains<ServerInputSystem>())
		{
			m_world.ctx().get<ServerInputSystem>().EnqueueInput(gameInput->user_id(), cmd);
		}
	}
}
