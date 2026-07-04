#include "pch.h"

#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/utils/ScopedTimer.h"

#include "jamnet/sync/networld/ServerPhysicalWorld.h"

#include "jamnet/sync/transport/CustomPacketHelper.h"

#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/WorldContext.h"
#include "jamnet/sync/replication/ServerInputSystem.h"
#include "jamnet/sync/replication/ServerPhysicsSystem.h"
#include "jamnet/sync/replication/ServerAoiSystem.h"
#include "jamnet/sync/replication/ServerReplicationSystem.h"

#include "jamnet/runtime/ServerSession.h"

#include "jamnet/sync/schema/gen/actor_spawn_generated.h"
#include "jamnet/sync/schema/gen/input_generated.h"

namespace jam::net
{

	namespace
	{
		Packet ClonePacket(const Packet& packet)
		{
			if (!packet.IsValid())
				return {};

			const uint32 size = packet->Size();
			BufWriter writer(GetNetBufferPool(eNetBufferPoolKind::Clone));
			BufferSlice slice = writer.OpenForPayload(size, alignof(PacketHeader));
			WritePayload(slice, packet->Head(), size);
			slice.Close();
			return MakeOwned(slice);
		}

		eProtocolType ResolveProtocol(const Packet& packet)
		{
			if (!packet.IsValid())
				return eProtocolType::NONE;

			const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
			if (!view.IsValid())
				return eProtocolType::NONE;

			return IsTcp(view.Channel()) ? eProtocolType::TCP : eProtocolType::UDP;
		}

		Session* SelectSession(const ServerSessionBundle& sessions, eProtocolType protocol)
		{
			if (protocol == eProtocolType::TCP)
				return sessions.TryGetTcp();

			if (protocol == eProtocolType::UDP)
				return sessions.TryGetUdp();

			return nullptr;
		}

		void SendToSession(Session* session, Packet packet)
		{
			if (!session || !packet.IsValid() || !session->IsConnected())
				return;

			session->Send(std::move(packet));
		}

		px::ObjectId BindingTarget(
			entt::registry& world,
			const entt::entity e,
			NetId target,
			const std::unordered_map<NetId, entt::entity>& index)
		{
			if (!target.IsValid()) return px::INVALID_OBJ_ID;

			if (auto it = index.find(target); it != index.end())
			{
				const entt::entity candidate = it->second;
				if (candidate != entt::null && world.valid(candidate) && world.all_of<PhysicsSpawnedTag>(candidate))
				{
					const px::ObjectId resolved = MakeObjectId(candidate);
					world.emplace<TargetInfo>(e, TargetInfo{ .targetNetId = target, .targetObjId = resolved });
					return resolved;
				}
			}

			return px::INVALID_OBJ_ID;
		}


	} // anonymous namespace





	ServerPhysicalWorld::ServerPhysicalWorld(const WorldConfig& config)
		: PhysicalWorld(config)
	{
	}

	bool ServerPhysicalWorld::Init()
	{
		if (!WorldMembershipHost::Init()) return false;

		if (auto shard = m_shard.lock())
			m_bridge = std::make_unique<ShardJobBridge>(*shard);

		if (!m_physics) return false;

		m_netIdToEntity.clear();
		m_userToControlledEntity.clear();
		m_ownerByNetId.clear();
		m_controllerByNetId.clear();
		m_entityByNetId.clear();
		m_netIdByEntity.clear();

		m_physics->SetJobBridge(m_bridge.get());
		m_physics->Init();

		m_registry.ctx().emplace<ServerPhysicalWorld*>(this);
		m_registry.ctx().emplace<TickCounter>().Init();
		m_registry.ctx().emplace<ServerInputSystem>(m_registry).Init();
		m_registry.ctx().emplace<ServerPhysicsSystem>(m_registry, m_physics.get()).Init();
		m_registry.ctx().emplace<ServerAoiSystem>(m_registry, m_physics.get()).Init();
		m_registry.ctx().emplace<ServerReplicationSystem>(m_registry).Init();

		BootstrapLevelActors();
		
		return true;
	}

	void ServerPhysicalWorld::Start(uint64 dt_ns)
	{
		JAM_ASSERT(IsCurrentShardContext());

		auto& L = CurrentShardLocalChecked();
		RegisterShardSystemFn(L, { DOMAIN_PHYSICS, GetWorldLocalIndex(GetWorldId()) }, dt_ns, [this](ShardLocal&, uint64, uint64) { Tick(); });
	}

	void ServerPhysicalWorld::Resume(uint64 dt_ns)
	{
		Start(dt_ns);
	}

	void ServerPhysicalWorld::Stop()
	{
		JAM_ASSERT(IsCurrentShardContext());

		auto& L = CurrentShardLocalChecked();
		L.domainGroups.erase({ DOMAIN_PHYSICS, GetWorldLocalIndex(GetWorldId()) });
	}

	void ServerPhysicalWorld::SetPhysicsFacade(std::unique_ptr<px::IPhysicsFacade> physics)
	{
		m_physics = std::move(physics);
	}

	bool ServerPhysicalWorld::AddMember(WorldUserContext user)
	{
		return WorldMembershipHost::AddMember(user);
	}

	bool ServerPhysicalWorld::RemoveMember(uint64 userId)
	{
		return WorldMembershipHost::RemoveMember(userId);
	}

	void ServerPhysicalWorld::SendTo(Packet packet, uint64 userId)
	{
		if (!packet.IsValid())
		{
			JAMNET_LOG_WARN_LOC("send packet is invalid");
			return;
		}

		const eProtocolType protocol = ResolveProtocol(packet);
		if (protocol == eProtocolType::NONE)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		if (auto it = m_userContexts.find(userId); it != m_userContexts.end())
		{
			SendToSession(SelectSession(it->second.sessions, protocol), std::move(packet));
		}
	}

	void ServerPhysicalWorld::Multicast(Packet packet)
	{
		if (!packet.IsValid())
			return;

		const eProtocolType protocol = ResolveProtocol(packet);
		if (protocol == eProtocolType::NONE)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		for (const auto& p : m_userContexts | std::views::values)
			SendToSession(SelectSession(p.sessions, protocol), ClonePacket(packet));
	}

	void ServerPhysicalWorld::UpdateMemberContext(WorldUserContext user)
	{
		WorldMembershipHost::UpdateMemberContext(user);
	}

	void ServerPhysicalWorld::RemoveMemberContext(uint64 userId)
	{
		WorldMembershipHost::RemoveMemberContext(userId);
	}

	bool ServerPhysicalWorld::DespawnActorImmediate(NetId netId, uint64 userId)
	{
		return DespawnActorImpl(netId, userId);
	}

	void ServerPhysicalWorld::HandleWorldPacket(uint64 callerUserId, Packet packet)
	{
		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());

		switch (view.Id())
		{
		case CustomPacketId::INPUT:
		{
			ProcessGameInput(callerUserId, view);
			break;
		}
		default: break;
		}
	}

	void ServerPhysicalWorld::SpawnActor(SpawnParams params)
	{
		JAM_ASSERT(IsCurrentShardContext());
		SpawnActorImpl(params);
	}

	void ServerPhysicalWorld::DespawnActor(const NetId netId, const uint64 userId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		DespawnActorImpl(netId, userId);
	}

	void ServerPhysicalWorld::PossessActor(NetId netId, uint64 userId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		PossessActorImpl(netId, userId);
	}

	void ServerPhysicalWorld::UnpossessActor(NetId netId, uint64 userId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		UnpossessActorImpl(netId, userId);
	}

	void ServerPhysicalWorld::SpawnActorAsync(SpawnParams params, std::function<void(NetId)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());

		NetId nid = SpawnActorImpl(params);
		JAMNET_LOG_DEBUG("[SpawnActorAsync] Spawn actor. net id= {}", nid.Raw());

		if (onDone)
			onDone(nid);
	}

	void ServerPhysicalWorld::DespawnActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());
		const bool ok = DespawnActorImpl(netId, userId);
		if (onDone)
			onDone(ok);
	}

	void ServerPhysicalWorld::PossessActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());
		const bool ok = PossessActorImpl(netId, userId);
		if (onDone)
			onDone(ok);
	}

	void ServerPhysicalWorld::UnpossessActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());
		const bool ok = UnpossessActorImpl(netId, userId);
		if (onDone)
			onDone(ok);
	}

	void ServerPhysicalWorld::RequestInitialSnapshotNewClient()
	{
		m_pendingInitialFullSnapshot.store(true, std::memory_order_relaxed);
	}

	entt::entity ServerPhysicalWorld::GetEntity(NetId netId) const
	{
		if (auto it = m_netIdToEntity.find(netId); it != m_netIdToEntity.end())
			return it->second;
		return entt::null;
	}

	entt::entity ServerPhysicalWorld::GetControlledEntity(uint64 userId) const
	{
		if (userId == 0)
			return entt::null;

		if (auto it = m_userToControlledEntity.find(userId); it != m_userToControlledEntity.end())
			return it->second;
		return entt::null;
	}



	void ServerPhysicalWorld::Tick()
	{
		if (!m_registry.ctx().contains<TickCounter>() 
			|| !m_registry.ctx().contains<ServerInputSystem>() 
			|| !m_registry.ctx().contains<ServerPhysicsSystem>()
			|| !m_registry.ctx().contains<ServerAoiSystem>()
			|| !m_registry.ctx().contains<ServerReplicationSystem>())
			return;

		m_registry.ctx().get<TickCounter>().Tick();
		m_registry.ctx().get<ServerInputSystem>().Tick();
		m_registry.ctx().get<ServerPhysicsSystem>().Tick();
		m_registry.ctx().get<ServerAoiSystem>().Tick();
		m_registry.ctx().get<ServerReplicationSystem>().Tick();
	}

	void ServerPhysicalWorld::OnUserJoined(uint64 userId)
	{
		if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>())
			aoi->OnUserEnter(userId);

		if (auto* repl = m_registry.ctx().find<ServerReplicationSystem>())
			repl->OnUserEnter(userId);
	}

	void ServerPhysicalWorld::OnUserLeft(uint64 userId)
	{
		m_userToControlledEntity.erase(userId);

		if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>())
			aoi->OnUserLeave(userId);

		if (auto* repl = m_registry.ctx().find<ServerReplicationSystem>())
			repl->OnUserLeave(userId);
	}

	void ServerPhysicalWorld::BootstrapLevelActors()
	{
		if (!m_physics || m_actorLevels.instances.empty())
			return;

		for (const ActorLevelInstanceData& instance : m_actorLevels.instances)
		{
			const ActorArchetypeData* actorArchetype = m_actorArchetypes.Find(instance.actorArchetype);
			if (!actorArchetype || !IsValidAssetKey(actorArchetype->key) || !IsValidAssetKey(actorArchetype->physicsArchetype))
				continue;

			const px::PhysicsArchetypeKey physicsArchetypeKey = actorArchetype->physicsArchetype;
			if (!IsValidAssetKey(physicsArchetypeKey))
				continue;

			const px::eBodyType bodyType = m_physics->FindBodyType(physicsArchetypeKey);
			if (bodyType == px::eBodyType::None)
				continue;

			const entt::entity e = m_registry.create();
			const NetId nid = NetId::MakeLevel(instance.levelActorId);
			if (!nid.IsValid())
			{
				m_registry.destroy(e);
				continue;
			}

			px::SpawnDesc desc{};
			desc.archetype = physicsArchetypeKey;
			desc.pose = instance.pose;
			desc.spawnSrc = px::eSpawnSource::Level;
			desc.overrides = (bodyType == px::eBodyType::Character)
				? std::variant<px::RigidSpawnOverrides, px::CharacterSpawnOverrides>(px::CharacterSpawnOverrides{})
				: std::variant<px::RigidSpawnOverrides, px::CharacterSpawnOverrides>(px::RigidSpawnOverrides{});

			m_registry.emplace<NetId>(e, nid);
			m_registry.emplace<NetActorBodyType>(e, NetActorBodyType{ bodyType });
			m_registry.emplace<NetTeamPartRole>(e);
			m_registry.emplace<NetActorArchetypeKey>(e, NetActorArchetypeKey{ actorArchetype->key });
			m_registry.emplace<NetPhysicsArchetypeKey>(e, NetPhysicsArchetypeKey{ physicsArchetypeKey });
			m_registry.emplace<OwnershipTag>(e);
			m_registry.emplace<ControlTag>(e);

			if (bodyType == px::eBodyType::Character)
			{
				px::CharacterState state{};
				state.pos = instance.pose.p;
				m_registry.emplace<CharAuthorityState>(e, state);
			}
			else
			{
				px::RigidState state{};
				state.pose = instance.pose;
				m_registry.emplace<RigidAuthorityState>(e, state);
			}

			m_registry.emplace<NewlyCreatedTag>(e);
			m_netIdToEntity[nid] = e;
			if (m_physics->FindMotionType(physicsArchetypeKey) == px::eMotionType::Static)
				m_registry.emplace<ReplicationStaticTag>(e);

			m_registry.ctx().get<ServerPhysicsSystem>().SpawnActor(e, desc);
			if (!m_registry.valid(e))
				continue;

			if (!m_registry.all_of<PhysicsSpawnedTag>(e) && !m_physics->IsStepPending())
			{
				m_netIdToEntity.erase(nid);
				m_registry.destroy(e);
				continue;
			}

			if (m_registry.all_of<PhysicsSpawnedTag>(e))
			{
				if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>())
					aoi->OnActorSpawned(e);
			}
		}
	}

	NetId ServerPhysicalWorld::SpawnActorImpl(SpawnParams params)
	{
		if (!IsValidAssetKey(params.actorArchetypeKey))
			return NetId::Invalid();

		if (!IsValidAssetKey(params.desc.archetype))
		{
			const ActorArchetypeData* actorArchetype = m_actorArchetypes.Find(params.actorArchetypeKey);
			if (!actorArchetype || !IsValidAssetKey(actorArchetype->physicsArchetype))
				return NetId::Invalid();

			params.desc.archetype = actorArchetype->physicsArchetype;
		}

		if (!IsValidAssetKey(params.desc.archetype))
			return NetId::Invalid();

		const entt::entity e = m_registry.create();

		const NetId nid = NetId::MakeRuntime(m_netIdGenerator.fetch_add(1, std::memory_order_relaxed));
		if (!nid.IsValid()) return NetId::Invalid();

		const px::eBodyType body = params.desc.IsRigid() ? px::eBodyType::Rigid : px::eBodyType::Character;

		m_registry.emplace<NetId>(e, nid);
		m_registry.emplace<NetActorBodyType>(e, NetActorBodyType{ body });
		m_registry.emplace<NetTeamPartRole>(e, NetTeamPartRole{ .team = params.desc.team, .part = params.desc.part, .role = params.desc.role });
		m_registry.emplace<NetActorArchetypeKey>(e, NetActorArchetypeKey{ params.actorArchetypeKey });
		m_registry.emplace<NetPhysicsArchetypeKey>(e, NetPhysicsArchetypeKey{ params.desc.archetype });
		m_registry.emplace<OwnershipTag>(e, OwnershipTag{ params.owner });
		m_registry.emplace<ControlTag>(e, ControlTag{ params.controller });
		
		if (body == px::eBodyType::Rigid)
		{
			m_registry.emplace<RigidAuthorityState>(e);
		}
		else
		{
			m_registry.emplace<CharAuthorityState>(e);
			if (params.owner && params.owner == params.controller)
				m_registry.emplace<px::CharacterInput>(e);
		}

		m_registry.emplace<NewlyCreatedTag>(e);
		m_registry.emplace<NetSpawnRequestId>(e, NetSpawnRequestId{ params.spawnId });


		m_netIdToEntity[nid] = e;
		if (params.controller != 0)
			m_userToControlledEntity[params.controller] = e;

		params.desc.targetId = BindingTarget(m_registry, e, params.targetNetId, m_netIdToEntity);

		m_registry.ctx().get<ServerPhysicsSystem>().SpawnActor(e, params.desc);
		if (m_registry.all_of<PhysicsSpawnedTag>(e))
		{
			if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>())
				aoi->OnActorSpawned(e);
		}


		return nid;
	}


	bool ServerPhysicalWorld::DespawnActorImpl(NetId netId, uint64 userId)
	{
		const entt::entity targetEntity = GetEntity(netId);

		if (targetEntity == entt::null)
			return false;

		if (auto* ownership = m_registry.try_get<OwnershipTag>(targetEntity))
		{
			if (userId != 0 && ownership->userId != userId)
				return false;
		}

		if (auto* repl = m_registry.ctx().find<ServerReplicationSystem>())
			repl->OnActorDestroyed(targetEntity);
		if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>())
			aoi->OnActorDestroyed(targetEntity);

		if (const auto* control = m_registry.try_get<ControlTag>(targetEntity))
		{
			if (control->userId != 0)
			{
				auto it = m_userToControlledEntity.find(control->userId);
				if (it != m_userToControlledEntity.end() && it->second == targetEntity)
					m_userToControlledEntity.erase(it);
			}
		}

		m_netIdToEntity.erase(netId);

		if (m_registry.all_of<PhysicsSpawnedTag>(targetEntity))
		{
			if (auto* phys = m_registry.ctx().find<ServerPhysicsSystem>())
			{
				phys->DespawnActor(targetEntity);
			}
			else
			{
				return false;
			}
		}

		m_registry.destroy(targetEntity);

		return true;
	}

	bool ServerPhysicalWorld::PossessActorImpl(NetId netId, uint64 userId)
	{
		const entt::entity target = GetEntity(netId);

		if (target == entt::null || !m_registry.valid(target) || !m_registry.all_of<OwnershipTag>(target))
			return false;

		auto& ownership = m_registry.get<OwnershipTag>(target);
		if (ownership.userId != userId)
			return false;

		auto* control = m_registry.try_get<ControlTag>(target);
		if (control && control->userId != 0 && control->userId != userId)
			return false;

		const entt::entity prevControlled = GetControlledEntity(userId);
		if (prevControlled != entt::null && prevControlled != target && m_registry.valid(prevControlled) && m_registry.all_of<ControlTag>(prevControlled))
		{
			auto& controlled = m_registry.get<ControlTag>(prevControlled);
			if (controlled.userId == userId)
			{
				controlled.userId = 0;
				m_registry.remove<px::CharacterInput>(prevControlled);

				if (auto* repl = m_registry.ctx().find<ServerReplicationSystem>())
					repl->MarkActorDirty(prevControlled, true);
			}
		}

		m_registry.emplace_or_replace<ControlTag>(target, ControlTag{ userId });
		m_registry.emplace_or_replace<px::CharacterInput>(target);
		m_userToControlledEntity[userId] = target;

		if (auto* repl = m_registry.ctx().find<ServerReplicationSystem>())
			repl->MarkActorDirty(target, true);

		return true;
	}

	bool ServerPhysicalWorld::UnpossessActorImpl(NetId netId, uint64 userId)
	{
		const entt::entity targetEntity = GetEntity(netId);

		if (targetEntity == entt::null || !m_registry.valid(targetEntity) || !m_registry.all_of<ControlTag>(targetEntity))
			return false;

		auto& controllable = m_registry.get<ControlTag>(targetEntity);

		// 권한 검증
		if (controllable.userId != userId)
			return false;

		// 조종 해제
		controllable.userId = 0;
		m_registry.remove<px::CharacterInput>(targetEntity);
		if (auto it = m_userToControlledEntity.find(userId); it != m_userToControlledEntity.end() && it->second == targetEntity)
			m_userToControlledEntity.erase(it);

		if (auto* repl = m_registry.ctx().find<ServerReplicationSystem>())
			repl->MarkActorDirty(targetEntity, true);

		return true;
	}

	void ServerPhysicalWorld::ProcessGameInput(uint64 callerUserId, const PacketHeaderView& pkt)
	{
		flatbuffers::Verifier verfier(pkt.Payload(), pkt.PayloadSize());
		if (!fb::VerifyfbGameInputBuffer(verfier)) return;

		const auto* gameInput = fb::GetfbGameInput(pkt.Payload());
		if (!gameInput) return;
		if (callerUserId == 0)
			return;

		if (gameInput->user_id() != 0 && gameInput->user_id() != callerUserId)
		{
			JAMNET_LOG_WARN("Ignoring claimed input userId={} and using authenticated principal={}",
				gameInput->user_id(), callerUserId);
		}

		InputCmd cmd{};
		cmd.seq					= gameInput->sequence();
		cmd.input.inputFlags	= gameInput->flags();
		cmd.input.commandEpoch	= gameInput->command_epoch();
		cmd.input.facingYaw		= gameInput->yaw();
		cmd.input.facingPitch	= gameInput->pitch();
		cmd.input.moveMode		= static_cast<px::eMoveInputMode>(gameInput->move_mode());
		cmd.input.mouseMoveKind = static_cast<px::eMouseMoveKind>(gameInput->mouse_move_kind());
		cmd.input.targetPos		= px::Vec3(gameInput->target_x(), gameInput->target_y(), gameInput->target_z());
		cmd.input.targetNetId	= gameInput->target_net_id();

		if (m_registry.ctx().contains<ServerInputSystem>())
		{
			m_registry.ctx().get<ServerInputSystem>().EnqueueInput(callerUserId, cmd);
		}
	}
}
