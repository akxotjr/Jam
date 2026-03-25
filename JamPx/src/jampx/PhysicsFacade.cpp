#include "pch.h"
#include "jampx/PhysicsFacade.h"

#include <algorithm>
#include <ranges>

#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/actor/ActorFactory.h"
#include "jampx/actor/character/controller/PlayerControllerComponent.h"
#include "jampx/actor/rigid/projectile/ProjectileRigidBehavior.h"

namespace jam::px
{
	namespace
	{


		TemplateHandle ToTemplateHandle(PrefabKey k)
		{
			return PHYSICS_PREFAB_REGISTRY.FindHandleByKey(k);
		}

	}


	void PhysicsFacade::Init()
	{
		if (m_inited.load(std::memory_order_relaxed)) return;

		m_world = std::make_unique<PhysicsWorld>();
		m_world->Init(m_dispacter.get());

		m_taskManager = m_world->GetTaskManager();

		m_levelLoader = std::make_unique<PrefabLevelLoader>();
		m_levelLoader->SetPhysicsWorld(m_world.get());

		m_inited.store(true, std::memory_order_relaxed);
	}

	void PhysicsFacade::Shutdown()
	{
		if (!m_inited.load(std::memory_order_relaxed)) return;

		if (m_world) m_world->Destroy();

		m_inited.store(false, std::memory_order_relaxed);
	}

	void PhysicsFacade::SetJobBridge(IPhysicsJobBridge* bridge)
	{
		JAM_ASSERT(!m_inited.load());
		m_bridge = bridge;
		if (bridge)
			m_dispacter = std::make_unique<ShardPxCpuDispacter>(*bridge);
	}


	LevelLayerInfo PhysicsFacade::SetLevelPath(const std::string& path)
	{
		if (m_levelLoader) return m_levelLoader->SetLevelPath(path);
		return {};
	}

	bool PhysicsFacade::LoadLevel(const std::string& layer, INOUT std::vector<LevelInstanceInfo>& instances)
	{
		if (!m_levelLoader) return false;

		auto results = m_levelLoader->Load(layer, instances);
		if (results.empty()) return false;

		for (auto& result : results)
		{
			if (result.bodyType != eBodyType::Rigid)
				return false;

			switch (result.motionType)
			{
			case eMotionType::Static:
			case eMotionType::Dynamic:
				m_rigidMap.emplace(result.id, std::move(result.body));
				break;

			case eMotionType::Kinematic:
				if (result.actorType == eActorType::Projectile)
					m_projectileMap.emplace(result.id, std::move(result.body));
				else
					m_kinematicMap.emplace(result.id, std::move(result.body));
				break;

			default:
				return false;
			}
		}
		return true;
	}

	std::vector<ObjectId> PhysicsFacade::UnloadLevel(const std::string& layer)
	{
		if (!m_levelLoader) return {};

		auto ids = m_levelLoader->Unload(layer);
		for (ObjectId id : ids) Despawn(id);

		return ids;
	}

	std::vector<ObjectId> PhysicsFacade::UnloadAllLevel()
	{
		if (!m_levelLoader) return {};

		auto ids = m_levelLoader->UnloadAll();
		for (ObjectId id : ids) Despawn(id);

		return ids;
	}

	bool PhysicsFacade::IsStepPending() const
	{
		return m_stepPending;
	}

	void PhysicsFacade::Simulate(float dt)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return;

		StepCharacters(ePxSceneSlot::Main, dt);
		StepKinematics(ePxSceneSlot::Main, dt);
		StepProjectiles(ePxSceneSlot::Main, dt);
		m_world->Simulate(ePxSceneSlot::Main, dt);
	}

	bool PhysicsFacade::BeginSimulate(float dt, uint64 awaitKey)
	{
		if (!m_world) return false;

		StepCharacters(ePxSceneSlot::Main, dt);
		StepKinematics(ePxSceneSlot::Main, dt);
		StepProjectiles(ePxSceneSlot::Main, dt);

		if (!m_dispacter || awaitKey == 0)
		{
			m_world->Simulate(ePxSceneSlot::Main, dt);
			return false;
		}

		m_completionTask.SetPhysicsJobBridge(m_bridge);
		m_completionTask.SetAwaitKey(awaitKey);
		m_completionTask.setContinuation(*m_taskManager, nullptr);

		m_world->BeginSimulate(ePxSceneSlot::Main, dt, &m_completionTask);

		m_completionTask.removeReference();

		m_stepPending = true;
		return true; // Suspend 해야 함을 알림
	}

	void PhysicsFacade::EndSimulate()
	{
		if (!m_world || !m_stepPending) return;
		m_world->EndSimulate(ePxSceneSlot::Main);
		m_stepPending = false;

		FlushPendingSceneOps();

		SyncKinematics(ePxSceneSlot::Main);
		SyncProjectiles(ePxSceneSlot::Main);
	}

	void PhysicsFacade::Resimulate(float dt)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return;

		StepCharacters(ePxSceneSlot::Replay, dt);
		StepKinematics(ePxSceneSlot::Replay, dt);
		StepProjectiles(ePxSceneSlot::Replay, dt);

		m_world->Simulate(ePxSceneSlot::Replay, dt);
	}

	bool PhysicsFacade::BeginResimulate(float dt, uint64 awaitKey)
	{
		if (!m_world) return false;

		StepCharacters(ePxSceneSlot::Replay, dt);
		StepKinematics(ePxSceneSlot::Replay, dt);
		StepProjectiles(ePxSceneSlot::Replay, dt);

		if (!m_dispacter || awaitKey == 0)
		{
			m_world->Simulate(ePxSceneSlot::Replay, dt);
			return false;
		}

		m_completionTask.SetPhysicsJobBridge(m_bridge);
		m_completionTask.SetAwaitKey(awaitKey);
		m_completionTask.setContinuation(*m_taskManager, nullptr);

		m_world->BeginSimulate(ePxSceneSlot::Replay, dt, &m_completionTask);

		m_completionTask.removeReference();

		m_stepPending = true;
		return true;
	}

	void PhysicsFacade::EndResimulate()
	{
		if (!m_world || !m_stepPending) return;
		m_world->EndSimulate(ePxSceneSlot::Replay);
		m_stepPending = false;

		FlushPendingSceneOps();

		SyncKinematics(ePxSceneSlot::Replay);
		SyncProjectiles(ePxSceneSlot::Replay);
	}

	bool PhysicsFacade::Spawn(ObjectId id, const SpawnDesc& desc)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return false;

		if (m_stepPending)
		{
			m_pendingSceneOps.push_back(PendingSceneOp{ 
				.type = ePendingSceneOpType::Spawn, 
				.id   = id, 
				.desc = desc });
			return true;
		}

		return SpawnNow(id, desc);
	}

	bool PhysicsFacade::Despawn(ObjectId id)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return false;

		if (m_stepPending)
		{
			m_pendingSceneOps.push_back(PendingSceneOp{
				.type = ePendingSceneOpType::Despawn, 
				.id   = id });
			return true;
		}

		return DespawnNow(id);
	}

	eBodyType PhysicsFacade::GetBodyType(ObjectId id) const
	{
		if (m_rigidMap.contains(id))		return eBodyType::Rigid;
		if (m_kinematicMap.contains(id))	return eBodyType::Rigid;
		if (m_projectileMap.contains(id))	return eBodyType::Rigid;
		if (m_cctMap.contains(id))			return eBodyType::Character;
		if (m_remoteCctMap.contains(id))	return eBodyType::Character;
		return eBodyType::None;
	}

	eBodyType PhysicsFacade::FindBodyType(PrefabKey key) const
	{
		return PHYSICS_PREFAB_REGISTRY.GetBodyType(key);
	}


	eMotionType PhysicsFacade::GetMotionType(ObjectId id) const
	{
		if (m_rigidMap.contains(id))
		{
			const auto* actor = m_rigidMap.at(id).GetMainActor();
			if (!actor) return eMotionType::None;
			if (actor->is<PxRigidStatic>()) return eMotionType::Static;
			return actor->is<PxRigidDynamic>() ? eMotionType::Dynamic : eMotionType::None;
		}

		if (m_kinematicMap.contains(id))	return eMotionType::Kinematic;
		if (m_projectileMap.contains(id))	return eMotionType::Kinematic;
		if (m_cctMap.contains(id))			return eMotionType::CCT;
		if (m_remoteCctMap.contains(id))	return eMotionType::RemoteCCT;

		return eMotionType::None;
	}

	eMotionType PhysicsFacade::FindMotionType(PrefabKey key) const
	{
		return PHYSICS_PREFAB_REGISTRY.GetMotionType(key);
	}

	bool PhysicsFacade::IsReplayCandidate(PrefabKey key) const
	{
		const auto* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(key);
		if (!def) return false;

		if (def->bodyType == eBodyType::Character)
			return true;

		if (def->bodyType != eBodyType::Rigid || !def->IsRigid())
			return false;

		const auto& rigidDef = std::get<RigidBodyDef>(def->body);
		for (ShapeHandle sh : rigidDef.shapes)
		{
			const ShapeDef& sd = PHYSICS_PREFAB_REGISTRY.GetShapeDef(sh);

			if (sd.simFD.category.has_any(SimCategory::CHARACTER))		return true;
			if (sd.simFD.mask.has_any(SimCategory::CHARACTER))			return true;
			if (sd.qryFD.category.has_any(QueryCategory::CHARACTER))	return true;
		}

		return false;
	}

	void PhysicsFacade::PushReplayStates(const std::vector<ActorContext>& contexts)
	{
		for (const auto& c : contexts)
		{
			if (const auto* rs = std::get_if<RigidState>(&c.state))
			{
				if (auto it = m_rigidMap.find(c.oid);	   it != m_rigidMap.end())		{ it->second.ApplyReplayState(*rs, false); continue; }
				if (auto it = m_kinematicMap.find(c.oid);  it != m_kinematicMap.end())	{ it->second.ApplyReplayState(*rs, true);  continue; }
				if (auto it = m_projectileMap.find(c.oid); it != m_projectileMap.end()) { it->second.ApplyReplayState(*rs, true);  continue; }
				continue;
			}

			if (const auto* cs = std::get_if<CharacterState>(&c.state))
			{
				if (auto it = m_cctMap.find(c.oid);		  it != m_cctMap.end())		  { it->second.ApplyAuthorityToReplay(*cs); continue; }
				if (auto it = m_remoteCctMap.find(c.oid); it != m_remoteCctMap.end()) { it->second.ApplyAuthorityToReplay(*cs); continue; }
			}
		}
	}
	
	void PhysicsFacade::PullCorrectionState(ObjectId oid, OUT CharacterState& state)
	{
		if (auto it = m_cctMap.find(oid); it != m_cctMap.end())
		{
			state = it->second.GetReplayState();
		}
	}

	void PhysicsFacade::PullPredictedState(ObjectId oid, OUT CharacterState& state)
	{
		if (auto it = m_cctMap.find(oid); it != m_cctMap.end())
		{
			state = it->second.GetMainState();
		}
	}

	void PhysicsFacade::PushAuthorityStates(const std::vector<ActorContext>& contexts)
	{
		for (const auto& c : contexts)
		{
			if (const auto* rs = std::get_if<RigidState>(&c.state))
			{
				if (auto it = m_rigidMap.find(c.oid);	   it != m_rigidMap.end())		{ it->second.ApplyMainState(*rs, false); MarkDirty(c.oid); continue; }
				if (auto it = m_kinematicMap.find(c.oid);  it != m_kinematicMap.end())	{ it->second.ApplyMainState(*rs, true);  MarkDirty(c.oid); continue; }
				if (auto it = m_projectileMap.find(c.oid); it != m_projectileMap.end()) { it->second.ApplyMainState(*rs, true);  MarkDirty(c.oid); continue; }
				continue;
			}

			if (const auto* cs = std::get_if<CharacterState>(&c.state))
			{
				if (auto it = m_cctMap.find(c.oid);		  it != m_cctMap.end())		  { it->second.ApplyAuthorityToMain(*cs); MarkDirty(c.oid); continue; }
				if (auto it = m_remoteCctMap.find(c.oid); it != m_remoteCctMap.end()) { it->second.ApplyAuthorityToMain(*cs); MarkDirty(c.oid); continue; }
			}
		}
	}

	void PhysicsFacade::PullProxyStates(OUT std::vector<ActorContext>& contexts)
	{
		auto fillOne = [this](ActorContext& c)
			{
				if (auto it = m_rigidMap.find(c.oid);	   it != m_rigidMap.end())		{ c.state = it->second.GetMainState(); return true; }
				if (auto it = m_kinematicMap.find(c.oid);  it != m_kinematicMap.end())	{ c.state = it->second.GetMainState(); return true; }
				if (auto it = m_projectileMap.find(c.oid); it != m_projectileMap.end()) { c.state = it->second.GetMainState(); return true; }
				if (auto it = m_cctMap.find(c.oid);		   it != m_cctMap.end())		{ c.state = it->second.GetMainState(); return true; }
				if (auto it = m_remoteCctMap.find(c.oid);  it != m_remoteCctMap.end())	{ c.state = it->second.GetMainState(); return true; }
				return false;
			};

		if (contexts.empty())
		{
			contexts.reserve(m_rigidMap.size() + m_kinematicMap.size() + m_projectileMap.size() + m_cctMap.size() + m_remoteCctMap.size());

			for (const auto& [id, b] : m_rigidMap)		{ ActorContext c{}; c.oid = id; c.state = b.GetMainState(); contexts.push_back(c); }
			for (const auto& [id, b] : m_kinematicMap)	{ ActorContext c{}; c.oid = id; c.state = b.GetMainState(); contexts.push_back(c); }
			for (const auto& [id, b] : m_projectileMap) { ActorContext c{}; c.oid = id; c.state = b.GetMainState(); contexts.push_back(c); }
			for (const auto& [id, b] : m_cctMap)		{ ActorContext c{}; c.oid = id; c.state = b.GetMainState(); contexts.push_back(c); }
			for (const auto& [id, b] : m_remoteCctMap)	{ ActorContext c{}; c.oid = id; c.state = b.GetMainState(); contexts.push_back(c); }
			return;
		}

		for (auto& c : contexts)
			fillOne(c);
	}


	bool PhysicsFacade::GetCharacterState(ObjectId id, CharacterState& state) const
	{
		if (auto it = m_cctMap.find(id); it != m_cctMap.end())
		{
			state = it->second.GetMainState();
			return true;
		}
		if (auto it = m_remoteCctMap.find(id); it != m_remoteCctMap.end())
		{
			state = it->second.GetMainState();
			return true;
		}
		return false;
	}

	bool PhysicsFacade::SetCharacterState(ObjectId id, const CharacterState& state)
	{
		if (auto it = m_cctMap.find(id); it != m_cctMap.end())
		{
			it->second.ApplyAuthorityToMain(state);
			MarkDirty(id);
			return true;
		}
		if (auto it = m_remoteCctMap.find(id); it != m_remoteCctMap.end())
		{
			it->second.ApplyAuthorityToMain(state);
			MarkDirty(id);
			return true;
		}
		return false;
	}

	bool PhysicsFacade::GetRigidState(ObjectId id, RigidState& state) const
	{
		if (auto it = m_rigidMap.find(id); it != m_rigidMap.end())
		{
			state = it->second.GetMainState();
			return true;
		}
		if (auto it = m_kinematicMap.find(id); it != m_kinematicMap.end())
		{
			state = it->second.GetMainState();
			return true;
		}
		if (auto it = m_projectileMap.find(id); it != m_projectileMap.end())
		{
			state = it->second.GetMainState();
			return true;
		}

		return false;
	}

	bool PhysicsFacade::SetRigidState(ObjectId id, const RigidState& state)
	{
		if (auto it = m_rigidMap.find(id); it != m_rigidMap.end())
		{
			it->second.ApplyMainState(state, false);
			return true;
		}
		if (auto it = m_kinematicMap.find(id); it != m_kinematicMap.end())
		{
			it->second.ApplyMainState(state, true);
			return true;
		}
		if (auto it = m_projectileMap.find(id); it != m_projectileMap.end())
		{
			it->second.ApplyMainState(state, true);
			return true;
		}
		return false;
	}


	void PhysicsFacade::ApplyCharacterInput(ObjectId id, const CharacterInput& input)
	{
		auto it = m_cctMap.find(id);
		if (it == m_cctMap.end()) return;

		CharacterBody& body = it->second;
		body.SetPlayerInput(input);
		MarkDirty(id);
	}

	bool PhysicsFacade::RaycastLOS(const Vec3& from, const Vec3& to) const
	{
		if (!m_world) return true;
		PxScene* scene = m_world->GetScene(ePxSceneSlot::Main);
		if (!scene) return true;

		const PxVec3 origin = ToPhysX(from);
		PxVec3 dir = ToPhysX(to - from);
		const float  dist = dir.magnitude();
		if (dist < 1e-3f) return true;
		dir /= dist;

		RequestQueryFD reqFD = MakeRequestQueryFD(
			QueryCategory::WORLD,
			0, QuerySublayer::LOS, 0,
			0, 0, 0,
			RequestQueryFlag::IGNORE_TRIGGERS
		);

		QueryFilterCallbackT<> cb{ DefaultQueryPolicy{}, k_LOSQueryHitTypeMap };

		const PxQueryFilterData fd = MakePxQueryFilterData(reqFD);

		PxRaycastBuffer buf;
		return !scene->raycast(origin, dir, dist, buf, PxHitFlag::eDEFAULT, fd, &cb);
	}


	HitscanResult PhysicsFacade::Hitscan(const Vec3& from, const Vec3& dir, float maxRange, uint16 teamId) const
	{
		HitscanResult result{};
		if (!m_world) return result;

		PxScene* scene = m_world->GetScene(ePxSceneSlot::Main);
		if (!scene) return result;

		const PxVec3 origin = ToPhysX(from);
		const PxVec3 pxDir = ToPhysX(dir.GetNormalized());

		RequestQueryFD reqFD = MakeRequestQueryFD(
			QueryCategory::CHARACTER | QueryCategory::HITBOX | QueryCategory::WORLD,
			0, QuerySublayer::Default, 0,
			teamId, 0, 0,
			RequestQueryFlag::IGNORE_TRIGGERS | RequestQueryFlag::IGNORE_SAME_TEAM
		);
		QueryFilterCallbackT<> cb{ DefaultQueryPolicy{}, k_LOSQueryHitTypeMap };
		const PxQueryFilterData fd = MakePxQueryFilterData(reqFD);

		PxRaycastBuffer buf;
		if (!scene->raycast(origin, pxDir, maxRange, buf, PxHitFlag::eDEFAULT, fd, &cb))
			return result;

		result.hit      = true;
		result.position = ToPx(buf.block.position);
		result.normal   = ToPx(buf.block.normal);
		result.hitId    = GetObjectId(buf.block.actor);

		return result;
	}

	std::vector<SimEvent> PhysicsFacade::ConsumeSimEvents()
	{
		if (!m_world) return {};

		auto events = m_world->ConsumeSimEvents();

		// ANALYTIC 투사체 히트 등 수동 push 이벤트 병합
		if (!m_pendingSimEvents.empty())
		{
			events.insert(events.end(), m_pendingSimEvents.begin(), m_pendingSimEvents.end());
			m_pendingSimEvents.clear();
		}

		return events;
	}

	std::vector<ObjectId> PhysicsFacade::PopActiveList()
	{
		if (!m_world) return {};

		auto list = m_world->ConsumeAdvancdActive();

		list.reserve(list.size() + m_dirtySet.size());
		for (const ObjectId id : m_dirtySet)
			list.push_back(id);

		m_dirtySet.clear();

		std::ranges::sort(list);
		list.erase(std::ranges::unique(list).begin(), list.end());

		return list;
	}

	void PhysicsFacade::MarkDirty(ObjectId id)
	{
		m_dirtySet.insert(id);
	}

	void PhysicsFacade::FlushPendingSceneOps()
	{
		if (!m_world || m_pendingSceneOps.empty())
			return;

		auto pending = std::move(m_pendingSceneOps);
		m_pendingSceneOps.clear();

		for (const auto& op : pending)
		{
			if (op.type == ePendingSceneOpType::Spawn)
				SpawnNow(op.id, op.desc);
			else
				DespawnNow(op.id);
		}
	}

	bool PhysicsFacade::SpawnNow(ObjectId id, const SpawnDesc& desc)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world)
			return false;

		const TemplateHandle tpl = ToTemplateHandle(desc.prefab);
		const ActorTemplateDef* tplDef = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(tpl);
		if (!tplDef)
			return false;

		const auto actorType = tplDef->actorType;
		const auto bodyType = tplDef->bodyType;
		const auto motionType = tplDef->motionType;

		if (bodyType == eBodyType::Rigid)
		{
			auto resolver = [this](ObjectId oid) -> std::optional<PxTransform>
				{
					return ResolveTargetPose(oid);
				};

			auto body = ActorFactory::CreateRigidBody(*m_world, tpl, *tplDef, desc, id, resolver);
			if (!body.has_value()) return false;

			switch (motionType)
			{
			case eMotionType::Static:
			case eMotionType::Dynamic:
				m_rigidMap.emplace(id, std::move(body.value()));
				return true;

			case eMotionType::Kinematic:
				if (actorType == eActorType::Projectile)
					m_projectileMap.emplace(id, std::move(body.value()));
				else
					m_kinematicMap.emplace(id, std::move(body.value()));
				return true;

			default:
				break;
			}
		}
		else if (bodyType == eBodyType::Character)
		{
			auto body = ActorFactory::CreateCharacterBody(*m_world, tpl, *tplDef, desc, id);
			if (!body.has_value()) return false;

			if (motionType == eMotionType::CCT)
			{
				m_cctMap.emplace(id, std::move(body.value()));
				return true;
			}
			if (motionType == eMotionType::RemoteCCT)
			{
				m_remoteCctMap.emplace(id, std::move(body.value()));
				return true;
			}
		}

		JAM_ASSERT(false, "Unknown body type in template: {}", tplDef->name);
		return false;
	}

	bool PhysicsFacade::DespawnNow(ObjectId id)
	{
		if (!m_world) return false;

		if (auto it = m_rigidMap.find(id); it != m_rigidMap.end())
		{
			ActorFactory::DestroyRigidBody(*m_world, it->second);
			m_rigidMap.erase(it);
			return true;
		}
		if (auto it = m_kinematicMap.find(id); it != m_kinematicMap.end())
		{
			ActorFactory::DestroyRigidBody(*m_world, it->second);
			m_kinematicMap.erase(it);
			return true;
		}
		if (auto it = m_projectileMap.find(id); it != m_projectileMap.end())
		{
			ActorFactory::DestroyRigidBody(*m_world, it->second);
			m_projectileMap.erase(it);
			return true;
		}
		if (auto it = m_cctMap.find(id); it != m_cctMap.end())
		{
			ActorFactory::DestroyCharacterBody(*m_world, it->second);
			m_cctMap.erase(it);
			return true;
		}
		if (auto it = m_remoteCctMap.find(id); it != m_remoteCctMap.end())
		{
			ActorFactory::DestroyCharacterBody(*m_world, it->second);
			m_remoteCctMap.erase(it);
			return true;
		}

		return false;
	}

	void PhysicsFacade::StepCharacters(ePxSceneSlot slot, float dt)
	{
		for (auto& [id, body] : m_cctMap)
		{
			if (slot == ePxSceneSlot::Main)
			{
				body.TickOnMain(dt);
				MarkDirty(id);
			}
			else if (slot == ePxSceneSlot::Replay)
			{
				body.TickOnReplay(dt);
			}
			
		}
	}

	void PhysicsFacade::StepKinematics(ePxSceneSlot slot, float dt)
	{
		for (auto& body : m_kinematicMap | std::views::values)
		{
			if (slot == ePxSceneSlot::Main)
				body.TickOnMain(dt);
			else if (slot == ePxSceneSlot::Replay)
				body.TickOnReplay(dt);
		}
	}

	void PhysicsFacade::StepProjectiles(ePxSceneSlot slot, float dt)
	{
		if (m_projectileMap.empty()) return;

		std::vector<ObjectId> toRemove;
		toRemove.reserve(m_projectileMap.size());

		for (auto& [id, body] : m_projectileMap)
		{
			if (slot == ePxSceneSlot::Main)
			{
				body.TickOnMain(dt);
				MarkDirty(id);

				auto* proj = dynamic_cast<ProjectileRigidBehavior*>(body.GetBehavior());
				if (!proj) continue;

				const ProjectileHitResult& r = proj->GetLastHitResult();
				if (r.hit)
				{
					SimEvent e{};
					e.type = eSimEventType::ContactFound;
					e.contact0					= id;
					e.contact1					= r.hitId;
					e.contactPointCount			= 1;
					e.contactPoints[0].position = r.position;
					e.contactPoints[0].normal	= r.normal;
					m_pendingSimEvents.push_back(e);
				}

				if (proj->IsTerminated())
					toRemove.push_back(id);
			}
			else if (slot == ePxSceneSlot::Replay)
			{
				body.TickOnReplay(dt);
			}
		}

		for (ObjectId id : toRemove)
			Despawn(id);
	}

	void PhysicsFacade::SyncKinematics(ePxSceneSlot slot)
	{
		for (auto& [id, body] : m_kinematicMap)
		{
			if (slot == ePxSceneSlot::Main)
			{
				if (body.SyncMainState(body))
					MarkDirty(id);
			}
			else if (slot == ePxSceneSlot::Replay)
			{
				body.SyncReplayState(body);
			}
		}
	}

	void PhysicsFacade::SyncProjectiles(ePxSceneSlot slot)
	{
		for (auto& [id, body] : m_projectileMap)
		{
			if (slot == ePxSceneSlot::Main)
			{
				if (body.SyncMainState(body))
					MarkDirty(id);
			}
			else if (slot == ePxSceneSlot::Replay)
			{
				body.SyncReplayState(body);
			}
		}
	}

	std::optional<PxTransform> PhysicsFacade::ResolveTargetPose(ObjectId oid)
	{
		if (auto it = m_rigidMap.find(oid); it != m_rigidMap.end())
			return ToPhysX(it->second.GetMainState().pose);
		if (auto it = m_kinematicMap.find(oid); it != m_kinematicMap.end())
			return ToPhysX(it->second.GetMainState().pose);
		if (auto it = m_projectileMap.find(oid); it != m_projectileMap.end())
			return ToPhysX(it->second.GetMainState().pose);
		if (auto it = m_cctMap.find(oid); it != m_cctMap.end())
			return PxTransform(ToPhysX(it->second.GetMainState().pos));
		if (auto it = m_remoteCctMap.find(oid); it != m_remoteCctMap.end())
			return PxTransform(ToPhysX(it->second.GetMainState().pos));

		return std::nullopt;
	}
}
