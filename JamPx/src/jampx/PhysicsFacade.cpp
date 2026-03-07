#include "pch.h"
#include "jampx/PhysicsFacade.h"

#include <algorithm>
#include <ranges>

#include "jampx/PhysicsCore.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/prefab/PrefabLevelLoader.h"
#include "jampx/projectile/ProjectileMoveComponent.h"


namespace jam::px
{
	namespace
	{


		inline prefab::TemplateHandle ToTemplateHandle(PrefabKey k)
		{
			return PHYSICS_PREFAB_REGISTRY.FindHandleByKey(k);
		}

		inline MoveIntent ToMoveIntent(const CharacterInput& input)
		{
			MoveIntent intent{};
			intent.moveYaw = input.facingYaw;

			float x = 0.f, y = 0.f;

			if (HasInputFlag(input.inputFlags, INPUT_FORWARD))	y += 1.f;
			if (HasInputFlag(input.inputFlags, INPUT_BACKWARD))	y -= 1.f;
			if (HasInputFlag(input.inputFlags, INPUT_LEFT))		x -= 1.f;
			if (HasInputFlag(input.inputFlags, INPUT_RIGHT))		x += 1.f;

			Vec2 dir = Vec2(x, y);
			dir.Normalize();

			intent.moveX		 = dir.x;
			intent.moveY		 = dir.y;
			intent.moveMag		 = 1.0f;

			intent.gaitRequest	 = HasInputFlag(input.inputFlags, INPUT_SPRINT) ? eGait::Sprint : 
								   HasInputFlag(input.inputFlags, INPUT_RUN)    ? eGait::Run    : eGait::Walk;

			intent.stanceRequest = HasInputFlag(input.inputFlags, INPUT_PRONE)  ? eStance::Prone :
								   HasInputFlag(input.inputFlags, INPUT_CROUCH) ? eStance::Crouching : eStance::Standing;

			intent.jumpPressed	 = HasInputFlag(input.inputFlags, INPUT_JUMP);
			intent.dashPressed	 = HasInputFlag(input.inputFlags, INPUT_DASH);

			return intent;
		}


		inline void ApplySpawnPackedIdToActorShapes(const PxRigidActor& actor, std::optional<uint16_t> teamId, std::optional<uint8_t> partId, std::optional<uint8_t> roleId)
		{
			if (!teamId.has_value() && !partId.has_value() && !roleId.has_value())
				return;

			const PxU32 n = actor.getNbShapes();
			if (n == 0) return;

			std::vector<PxShape*> shapes;
			shapes.resize(n);
			actor.getShapes(shapes.data(), n);

			for (PxShape* s : shapes)
			{
				if (!s) continue;

				PxFilterData qfd = s->getQueryFilterData();

				PackedId32 old{};
				old.v = qfd.word2;

				const auto [v] = PackedId32::Make(
					teamId.value_or(old.Team()),
					partId.value_or(old.Part()),
					roleId.value_or(old.Role())
				);

				qfd.word2 = v;

				s->setQueryFilterData(qfd);
			}
		}



	}







	void PhysicsFacade::Init()
	{
		if (m_inited.load(std::memory_order_relaxed)) return;

		m_world = std::make_unique<PhysicsWorld>();
		m_world->Init(m_dispacter.get());

		m_taskManager = m_world->GetTaskManager();

		m_levelLoader = std::make_unique<prefab::PrefabLevelLoader>();
		m_levelLoader->SetPhysicsWorld(m_world.get());

		m_inited.store(true, std::memory_order_relaxed);
	}

	void PhysicsFacade::Shutdown()
	{
		if (!m_inited.load(std::memory_order_relaxed)) return;

		for (auto& e : m_rigidEntries | std::views::values)
		{
			if (e.actor) { m_world->RemoveActor(e.actor); e.actor = nullptr; }
		}
		m_rigidEntries.clear();

		for (auto& e : m_characterEntries | std::views::values)
		{
			if (e.controller) { m_world->RemoveController(e.controller); e.controller = nullptr; }
		}
		m_characterEntries.clear();

		if (m_world)
			m_world->Destroy();
	
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

		MoveCharacter(dt);
		StepProjectiles(dt);
		m_world->Simulate(dt);
	}

	bool PhysicsFacade::BeginStep(float dt, uint64 awaitKey)
	{
		if (!m_world) return false;

		MoveCharacter(dt);
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
	}

	PhysicsHandle PhysicsFacade::Spawn(ObjectId id, const SpawnDesc& desc)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return {};

		if (auto it = m_rigidEntries.find(id); it != m_rigidEntries.end())
			return it->second.physicsHandle;

		if (auto it = m_characterEntries.find(id); it != m_characterEntries.end())
			return it->second.physicsHandle;

		const prefab::TemplateHandle templateHandle = ToTemplateHandle(desc.prefab);

		const prefab::PrefabTemplateDef* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(templateHandle);
		if (!def) return {};

		void* userData = nullptr;

		if (def->actorType == eActorType::Character)
		{
			if (!desc.cs.has_value()) return {};

			const auto& cs = desc.cs.value();

			CharacterUserData ud{ .id = id };
			userData = reinterpret_cast<void*>(&ud);

			CharacterEntry entry{};
			entry.templateHandle = templateHandle;
			entry.isKinematic = desc.isKinematic;

			const auto created = m_world->CreateCharacter(templateHandle, ToPhysX(cs.pos), userData);
			if (!created.controller) return {};

			entry.controller = created.controller;
			entry.hitbox = created.hitboxActor;

			if (entry.hitbox)
				ApplySpawnPackedIdToActorShapes(*entry.hitbox, desc.teamId, desc.partId, desc.user8);

			entry.physicsHandle = PhysicsHandle{ reinterpret_cast<uint64_t>(entry.controller) };
			entry.mover = std::make_unique<CharacterMovementComponent>(def->cct.movement, entry.controller, entry.hitbox);
			entry.state = cs;

			SetCharacterState(id, cs);

			m_characterEntries.emplace(id, std::move(entry));
			return m_characterEntries[id].physicsHandle;
		}

		if (!desc.rs.has_value()) return {};
		const auto& rs = desc.rs.value();

		userData = reinterpret_cast<void*>(id);

		const bool applyKinematic = desc.isKinematic || (def->motionType == eMotionType::Kinematic);

		RigidEntry entry{};
		entry.templateHandle = templateHandle;
		entry.isKinematic = applyKinematic;
		entry.actor = m_world->CreateActor(templateHandle, ToPhysX(rs.pose), userData);
		if (!entry.actor) return {};

		if (applyKinematic)
		{
			if (auto* dyn = entry.actor->is<PxRigidDynamic>())
			{
				auto flags = dyn->getRigidBodyFlags();
				flags |= PxRigidBodyFlag::eKINEMATIC;
				dyn->setRigidBodyFlags(flags);
			}
		}

		ApplySpawnPackedIdToActorShapes(*entry.actor, desc.teamId, desc.partId, desc.user8);

		entry.physicsHandle = PhysicsHandle{ reinterpret_cast<uint64_t>(entry.actor) };
		entry.state = rs;

		SetRigidState(id, rs);

		m_rigidEntries.emplace(id, std::move(entry));
		return m_rigidEntries[id].physicsHandle;
	}

	void PhysicsFacade::Despawn(ObjectId id)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return;

		if (auto it = m_rigidEntries.find(id); it != m_rigidEntries.end())
		{
			RigidEntry& entry = it->second;
			if (entry.actor)
			{
				m_world->RemoveActor(entry.actor);
				entry.actor = nullptr;
			}

			m_rigidEntries.erase(it);
		}

		if (auto it = m_characterEntries.find(id); it != m_characterEntries.end())
		{
			CharacterEntry& entry = it->second;

			if (entry.hitbox)
			{
				m_world->RemoveActor(entry.hitbox);
				entry.hitbox = nullptr;
			}

			if (entry.controller)
			{
				m_world->RemoveController(entry.controller);
				entry.controller = nullptr;
			}

			m_characterEntries.erase(it);
		}
	}

	eActorType PhysicsFacade::GetActorType(ObjectId id) const
	{
		if (auto it = m_characterEntries.find(id); it != m_characterEntries.end())
		{
			if (it->second.controller)
				return eActorType::Character;
		}

		if (m_projectileEntries.contains(id))
			return eActorType::Projectile;

		if (auto it = m_rigidEntries.find(id); it != m_rigidEntries.end())
		{
			const auto* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(it->second.templateHandle);
			return def ? def->actorType : eActorType::Generic;
		}

		return eActorType::Generic;
	}

	eMotionType PhysicsFacade::GetMotionType(ObjectId id) const
	{
		if (auto it = m_rigidEntries.find(id); it != m_rigidEntries.end())
		{
			const RigidEntry& entry = it->second;
			if (entry.actor)
			{
				if (entry.actor->is<PxRigidStatic>())
					return eMotionType::Static;

				if (auto* dyn = entry.actor->is<PxRigidDynamic>())
				{
					if (dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
						return eMotionType::Kinematic;
					return eMotionType::Dynamic;
				}
			}
			return eMotionType::None;
		}

		if (auto it = m_characterEntries.find(id); it != m_characterEntries.end())
		{
			if (it->second.controller)
				return eMotionType::Kinematic;
		}

		return eMotionType::None;
	}

	eActorType PhysicsFacade::FindActorType(PrefabKey key) const
	{
		const prefab::TemplateHandle h = PHYSICS_PREFAB_REGISTRY.FindHandleByKey(key);
		const prefab::PrefabTemplateDef* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(h);
		return def ? def->actorType : eActorType::Generic;
	}

	eMotionType PhysicsFacade::FindMotionType(PrefabKey key) const
	{
		return PHYSICS_PREFAB_REGISTRY.GetMotionType(key);
	}


	bool PhysicsFacade::GetCharacterState(ObjectId id, CharacterState& state) const
	{
		auto it = m_characterEntries.find(id);
		if (it == m_characterEntries.end())
			return false;

		const CharacterEntry& e = it->second;
		if (!e.controller || !e.mover)
			return false;

		e.mover->GetCharacterState(state);
		return true;
	}

	bool PhysicsFacade::SetCharacterState(ObjectId id, const CharacterState& state)
	{
		auto it = m_characterEntries.find(id);
		if (it == m_characterEntries.end())
			return false;

		CharacterEntry& e = it->second;
		if (!e.controller) return false;

		// 1. CCT 위치 복원 + sense 캐시 무효화 (stale grounded 방지)
		e.mover->Teleport(state.pos);

		// 2. CharacterMovementComponent 내부 상태 복원
		if (e.mover)
		{
			CharacterMoveState mvState = e.mover->GetMoveState();

			// pos
			mvState.position = state.pos;

			// velocity 재구성: moveDir(XZ 방향) * horizontalSpeed + Y
			const float hSpd = state.horizontalSpeed;
			mvState.velocity.x = state.moveDir.x * hSpd;
			mvState.velocity.z = state.moveDir.y * hSpd;
			mvState.velocity.y = state.verticalSpeed;

			// air/grounded 복원
			const bool isJumping = HasStateFlag(state.stateFlags, STATE_IS_JUMPING);
			if (isJumping || state.verticalSpeed > 0.01f)
			{
				mvState.grounded = false;
				mvState.air = state.verticalSpeed > 0.f ? eAirState::Rising : eAirState::Falling;
			}
			else if (state.verticalSpeed < -0.01f)
			{
				mvState.grounded = false;
				mvState.air = eAirState::Falling;
			}
			else
			{
				mvState.grounded = true;
				mvState.air = eAirState::Grounded;
			}

			mvState.jump.coyoteRemain = 0.f;
			mvState.jump.bufferRemain = 0.f;

			e.mover->SetMoveState(mvState);
		}
		MarkDirty(id);

		return true;
	}

	bool PhysicsFacade::GetRigidState(ObjectId id, RigidState& state) const
	{
		auto it = m_rigidEntries.find(id);
		if (it == m_rigidEntries.end())
			return false;

		const RigidEntry& e = it->second;
		if (!e.actor) return false;

		state.pose = ToPx(e.actor->getGlobalPose());

		if (auto* dyn = e.actor->is<PxRigidDynamic>())
		{
			state.linVel = ToPx(dyn->getLinearVelocity());
			state.angVel = ToPx(dyn->getAngularVelocity());
		}
		else
		{
			state.linVel = {};
			state.angVel = {};
		}

		return true;
	}

	bool PhysicsFacade::SetRigidState(ObjectId id, const RigidState& state)
	{
		auto it = m_rigidEntries.find(id);
		if (it == m_rigidEntries.end())
			return false;

		RigidEntry& e = it->second;
		if (!e.actor) return false;
		e.state = state;

		if (e.isKinematic)
		{
			if (auto* dyn = e.actor->is<PxRigidDynamic>())
				dyn->setKinematicTarget(ToPhysX(state.pose));
			else
				e.actor->setGlobalPose(ToPhysX(state.pose)); // static fallback
		}
		else
		{
			e.actor->setGlobalPose(ToPhysX(state.pose));

			if (auto* dyn = e.actor->is<PxRigidDynamic>())
			{
				dyn->setLinearVelocity(ToPhysX(state.linVel));
				dyn->setAngularVelocity(ToPhysX(state.angVel));
			}
		}

		MarkDirty(id);

		return true;
	}

	void PhysicsFacade::ApplyCharacterInput(ObjectId id, const CharacterInput& input)
	{
		auto it = m_characterEntries.find(id);
		if (it == m_characterEntries.end()) return;

		CharacterEntry& entry = it->second;
		if (!entry.controller) return;

		entry.lastIntent		= ToMoveIntent(input);
		entry.state.facingYaw	= input.facingYaw;
		entry.state.facingPitch = input.facingPitch;
	}

	void PhysicsFacade::AttachKinematicDriver(ObjectId id, std::unique_ptr<IKinematicDriver> driver)
	{
		auto it = m_rigidEntries.find(id);
		if (it == m_rigidEntries.end() || !it->second.isKinematic) return;

		it->second.mover = std::make_unique<KinematicMoveComponent>(std::move(driver));
	}

	void PhysicsFacade::DetachKinematicDriver(ObjectId id)
	{
		auto it = m_rigidEntries.find(id);
		if (it == m_rigidEntries.end()) return;

		it->second.mover.reset();
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

	// todo
	void PhysicsFacade::MoveKinematic(ObjectId id, const Transform& target)
	{
		auto it = m_rigidEntries.find(id);
		if (it == m_rigidEntries.end()) return;

		RigidEntry& e = it->second;
		if (!e.isKinematic) return;

		if (auto* dyn = e.actor->is<PxRigidDynamic>())
		{
			dyn->setKinematicTarget(ToPhysX(target));
			e.state.pose = target;
			MarkDirty(id);
		}
	}

	PhysicsHandle PhysicsFacade::SpawnProjectile(ObjectId id, const ProjectileSpawnDesc& desc)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return {};
		if (m_projectileEntries.contains(id)) return m_projectileEntries[id].physicsHandle;
		if (desc.kind == eProjectileKind::HITSCAN) return {};

		const prefab::TemplateHandle templateHandle = ToTemplateHandle(desc.prefab);
		if (!PHYSICS_PREFAB_REGISTRY.HasTemplate(templateHandle)) return {};

		void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
		PxRigidActor* actor = m_world->CreateActor(templateHandle, ToPhysX(desc.pose), userData);
		if (!actor) return {};

		ProjectileEntry entry{};
		entry.kind = desc.kind;
		entry.templateHandle = templateHandle;
		entry.actor = actor;
		entry.teamId = desc.teamId;

		if (desc.kind == eProjectileKind::DYN_SIM)
		{
			if (auto* dyn = actor->is<PxRigidDynamic>())
			{
				dyn->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
				dyn->setLinearVelocity(ToPhysX(desc.velocity));
			}
			// mover 없음: PhysX onContact → SimEvent 로 처리
		}
		else // ANALYTIC
		{
			if (auto* dyn = actor->is<PxRigidDynamic>())
				dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

			ProjectileMoveConfig cfg{};
			cfg.gravityScale = desc.gravityScale;
			cfg.maxRange	 = desc.maxRange;

			entry.mover = std::make_unique<ProjectileMoveComponent>(cfg, desc.velocity);
		}

		entry.physicsHandle = PhysicsHandle{ reinterpret_cast<uint64_t>(actor) };

		m_projectileEntries.emplace(id, std::move(entry));
		MarkDirty(id);
		return m_projectileEntries[id].physicsHandle;
	}

	void PhysicsFacade::DespawnProjectile(ObjectId id)
	{
		auto it = m_projectileEntries.find(id);
		if (it == m_projectileEntries.end()) return;

		if (it->second.actor)
		{
			m_world->RemoveActor(it->second.actor);
			it->second.actor = nullptr;
		}
		m_projectileEntries.erase(it);
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

		ranges::sort(list);
		list.erase(ranges::unique(list).begin(), list.end());

		return list;
	}

	void PhysicsFacade::MoveCharacter(float dt)
	{
		for (auto& [id, entry] : m_characterEntries)
		{
			if (!entry.controller || !entry.mover || entry.isKinematic) continue;

			entry.mover->Tick(dt, entry.lastIntent);

			MarkDirty(id);
		}
	}

	void PhysicsFacade::MarkDirty(ObjectId id)
	{
		m_dirtySet.insert(id);
	}

	void PhysicsFacade::StepProjectiles(float dt)
	{
		if (m_projectileEntries.empty()) return;

		PxScene* scene = m_world ? m_world->GetScene() : nullptr;
		if (!scene) return;

		std::vector<ObjectId> toRemove;

		for (auto& [id, entry] : m_projectileEntries)
		{
			if (entry.kind != eProjectileKind::ANALYTIC) continue;
			if (!entry.actor || !entry.mover) continue;

			auto* dyn = entry.actor->is<PxRigidDynamic>();
			if (!dyn) continue;

			const auto result = entry.mover->Tick(dt, scene, dyn, entry.teamId);

			if (result.hit)
			{
				SimEvent e{};
				e.type						= eSimEventType::ContactFound;
				e.contact0					= id;
				e.contact1					= result.hitId;
				e.contactPointCount			= 1;
				e.contactPoints[0].position = ToPhysX(result.position);
				e.contactPoints[0].normal   = ToPhysX(result.normal);

				m_pendingSimEvents.push_back(e);
				toRemove.push_back(id);
				continue;
			}

			if (result.maxRangeReached)
			{
				toRemove.push_back(id);
				continue;
			}

			MarkDirty(id);
		}

		for (ObjectId rmId : toRemove)
			DespawnProjectile(rmId);
	}

	void PhysicsFacade::StepKinematics(float dt)
	{
		for (auto& [id, entry] : m_rigidEntries)
		{
			if (!entry.isKinematic || !entry.mover) continue;

			auto* dyn = entry.actor ? entry.actor->is<PxRigidDynamic>() : nullptr;
			if (!dyn) continue;

			const Transform target = entry.mover->Tick(dt);
			dyn->setKinematicTarget(ToPhysX(target));
			entry.state.pose = target;
			MarkDirty(id);
		}
	}
}
