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

	bool PhysicsFacade::LoadLevel(const std::string& path)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world || !m_levelLoader) return false;

		m_levelLoader->Load("default", path);
		return true;
	}

	void PhysicsFacade::Step(float dt)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return;

		StepCharacters(dt);
		StepProjectiles(dt);
		m_world->Simulate(dt);
	}

	bool PhysicsFacade::BeginStep(float dt, uint64 awaitKey)
	{
		if (!m_world) return false;

		StepCharacters(dt);
		StepProjectiles(dt);

		if (!m_dispacter || awaitKey == 0)
		{
			m_world->Simulate(dt);
			return false;
		}

		// Completion Task 설정
		m_completionTask.SetPhysicsJobBridge(m_bridge);
		m_completionTask.SetAwaitKey(awaitKey);
		m_completionTask.setContinuation(*m_taskManager, nullptr);

		// 시뮬레이션 시작 (완료 시 m_completionTask가 실행됨)
		m_world->BeginSimulate(dt, &m_completionTask);

		m_completionTask.removeReference();

		m_stepPending = true;
		return true; // Suspend 해야 함을 알림
	}

	void PhysicsFacade::EndStep()
	{
		if (!m_world || !m_stepPending) return;
		m_world->EndSimulate();
		m_stepPending = false;

		SyncKinematics();
		SyncProjectiles();
	}

	PhysicsHandle PhysicsFacade::Spawn(ObjectId id, const SpawnDesc& desc)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return {};

		const TemplateHandle	tpl		= ToTemplateHandle(desc.prefab);
		const ActorTemplateDef* tplDef	= PHYSICS_PREFAB_REGISTRY.FindTemplateDef(tpl);
		if (!tplDef) return {};

		const auto actorType  = tplDef->actorType;
		const auto bodyType   = tplDef->bodyType;
		const auto motionType = tplDef->motionType;

		if (bodyType == eBodyType::Rigid)
		{
			auto body = ActorFactory::CreateRigidBody(*m_world, tpl, *tplDef, desc, id);
			if (!body.has_value()) return {};

			switch (motionType)
			{
			case eMotionType::Static:
			case eMotionType::Dynamic:
				{
					auto [it, inserted] = m_rigidMap.emplace(id, std::move(body.value()));
					return it->second.GetPhysicsHandle();
				}

			case eMotionType::Kinematic:
				{
					if (actorType == eActorType::Projectile)
					{
						auto [it, inserted] = m_projectileMap.emplace(id, std::move(body.value()));
						return it->second.GetPhysicsHandle();
					}

					auto [it, inserted] = m_kinematicMap.emplace(id, std::move(body.value()));
					return it->second.GetPhysicsHandle();
				}

			default: break;
			}
		}
		else if (bodyType == eBodyType::Character)
		{
			auto body = ActorFactory::CreateCharacterBody(*m_world, tpl, *tplDef, desc, id);
			if (!body.has_value()) return {};

			if (motionType == eMotionType::CCT)
			{
				auto [it, inserted] = m_cctMap.emplace(id, std::move(body.value()));
				return it->second.GetPhysicsHandle();
			}
			if (motionType == eMotionType::RemoteCCT)
			{
				auto [it, inserted] = m_remoteCctMap.emplace(id, std::move(body.value()));
				return it->second.GetPhysicsHandle();
			}
		}

		JAM_ASSERT(false, "Unknown body type in template: {}", tplDef->name);
		return {};
	}

	void PhysicsFacade::Despawn(ObjectId id)
	{
		if (!m_world) return;

		if (auto it = m_rigidMap.find(id); it != m_rigidMap.end())
		{
			ActorFactory::DestroyRigidBody(*m_world, it->second);
			m_rigidMap.erase(it);
			return;
		}
		if (auto it = m_kinematicMap.find(id); it != m_kinematicMap.end())
		{
			ActorFactory::DestroyRigidBody(*m_world, it->second);
			m_kinematicMap.erase(it);
			return;
		}
		if (auto it = m_projectileMap.find(id); it != m_projectileMap.end())
		{
			ActorFactory::DestroyRigidBody(*m_world, it->second);
			m_projectileMap.erase(it);
			return;
		}
		if (auto it = m_cctMap.find(id); it != m_cctMap.end())
		{
			ActorFactory::DestroyCharacterBody(*m_world, it->second);
			m_cctMap.erase(it);
			return;
		}
		if (auto it = m_remoteCctMap.find(id); it != m_remoteCctMap.end())
		{
			ActorFactory::DestroyCharacterBody(*m_world, it->second);
			m_remoteCctMap.erase(it);
		}
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
			const auto* actor = m_rigidMap.at(id).GetActor();
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


	bool PhysicsFacade::GetCharacterState(ObjectId id, CharacterState& state) const
	{
		if (auto it = m_cctMap.find(id); it != m_cctMap.end())
		{
			state = it->second.GetState();
			return true;
		}
		if (auto it = m_remoteCctMap.find(id); it != m_remoteCctMap.end())
		{
			state = it->second.GetState();
			return true;
		}
		return false;
	}

	bool PhysicsFacade::SetCharacterState(ObjectId id, const CharacterState& state)
	{
		if (auto it = m_cctMap.find(id); it != m_cctMap.end())
		{
			it->second.SetState(state);
			MarkDirty(id);
			return true;
		}
		if (auto it = m_remoteCctMap.find(id); it != m_remoteCctMap.end())
		{
			it->second.SetState(state);
			MarkDirty(id);
			return true;
		}
		return false;
	}

	bool PhysicsFacade::GetRigidState(ObjectId id, RigidState& state) const
	{
		if (auto it = m_rigidMap.find(id); it != m_rigidMap.end())
		{
			state = it->second.GetState();
			return true;
		}
		if (auto it = m_kinematicMap.find(id); it != m_kinematicMap.end())
		{
			state = it->second.GetState();
			return true;
		}
		if (auto it = m_projectileMap.find(id); it != m_projectileMap.end())
		{
			state = it->second.GetState();
			return true;
		}

		return false;
	}

	bool PhysicsFacade::SetRigidState(ObjectId id, const RigidState& state)
	{
		if (auto it = m_rigidMap.find(id); it != m_rigidMap.end())
		{
			it->second.SetState(state, false);
			return true;
		}
		if (auto it = m_kinematicMap.find(id); it != m_kinematicMap.end())
		{
			it->second.SetState(state, true);
			return true;
		}
		if (auto it = m_projectileMap.find(id); it != m_projectileMap.end())
		{
			it->second.SetState(state, true);
			return true;
		}
		return false;
	}

	void PhysicsFacade::ApplyCharacterInput(ObjectId id, const CharacterInput& input)
	{
		auto it = m_cctMap.find(id);
		if (it == m_cctMap.end()) return;

		CharacterBody& body = it->second;

		if (auto* player = dynamic_cast<PlayerControllerComponent*>(body.GetBrain()))
			player->SetInput(input);

		// todo: 여기서 하는게 맞나?
		body.SetFacing(input.facingYaw, input.facingPitch);
		MarkDirty(id);
	}



	bool PhysicsFacade::RaycastLOS(const Vec3& from, const Vec3& to) const
	{
		if (!m_world) return true;
		PxScene* scene = m_world->GetScene();
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

		PxScene* scene = m_world->GetScene();
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

	void PhysicsFacade::StepCharacters(float dt)
	{
		for (auto& [id, body] : m_cctMap)
		{
			body.Tick(dt);
			MarkDirty(id);
		}
	}

	void PhysicsFacade::StepKinematics(float dt)
	{
		for (auto& [id, body] : m_kinematicMap)
		{
			body.Tick(dt);
			MarkDirty(id);
		}
	}

	void PhysicsFacade::StepProjectiles(float dt)
	{
		if (m_projectileMap.empty()) return;

		std::vector<ObjectId> toRemove;
		toRemove.reserve(m_projectileMap.size());

		for (auto& [id, body] : m_projectileMap)
		{
			body.Tick(dt);

			auto* proj = dynamic_cast<ProjectileRigidBehavior*>(body.GetBehavior());
			if (!proj) continue;

			MarkDirty(id);

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

		for (ObjectId id : toRemove)
			Despawn(id);
	}

	void PhysicsFacade::SyncKinematics()
	{
		for (auto& [id, body] : m_kinematicMap)
		{
			body.SyncState(body);
			MarkDirty(id);
		}
	}

	void PhysicsFacade::SyncProjectiles()
	{
		for (auto& [id, body] : m_projectileMap)
		{
			body.SyncState(body);
			MarkDirty(id);
		}
	}
}
