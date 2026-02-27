#include "pch.h"
#include "jampx/PhysicsFacade.h"

#include <ranges>

#include "jampx/PhysicsCore.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/prefab/PrefabLevelLoader.h"


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


		inline void ApplySpawnPackedIdToActorShapes(const PxRigidActor& actor, optional<uint16_t> teamId, optional<uint8_t> partId, optional<uint8_t> user8)
		{
			if (!teamId.has_value() && !partId.has_value() && !user8.has_value())
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
					user8.value_or(old.Role())
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
		JAMNET_ASSERT(!m_inited.load());
		m_bridge = bridge;
		if (bridge)
			m_dispacter = std::make_unique<ShardPxCpuDispacter>(*bridge);
	}

	bool PhysicsFacade::LoadLevel(const string& path)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world || !m_levelLoader) return false;

		m_levelLoader->Load("default", path);
		return true;
	}

	void PhysicsFacade::Step(float dt)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return;

		MoveCharacter(dt);
		m_world->Simulate(dt);
	}

	bool PhysicsFacade::BeginStep(float dt, uint64 awaitKey)
	{
		if (!m_world) return false;

		MoveCharacter(dt);

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

	PhysicsHandle PhysicsFacade::Spawn(ObjectKey key, const SpawnDesc& desc)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return {};

		if (auto it = m_rigidEntries.find(key); it != m_rigidEntries.end())
			return it->second.physicsHandle;

		if (auto it = m_characterEntries.find(key); it != m_characterEntries.end())
			return it->second.physicsHandle;

		const prefab::TemplateHandle templateHandle = ToTemplateHandle(desc.prefab);

		const prefab::PrefabTemplateDef* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(templateHandle);
		if (!def) return {};

		void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(key.value));

		if (def->kind == prefab::ePrefabBodyKind::CHARACTER)
		{
			if (!desc.cs.has_value()) return {};

			const auto& cs = desc.cs.value();

			CharacterEntry entry{};
			entry.templateHandle = templateHandle;
			entry.isKinematic    = desc.isKinematic;

			const auto created = m_world->CreateCharacter(templateHandle, ToPhysX(cs.pos), userData);
			if (!created.controller) return {};

			entry.controller  = created.controller;
			entry.hitboxActor = created.hitboxActor;

			// hitbox에도 PackedId32 적용
			if (entry.hitboxActor)
				ApplySpawnPackedIdToActorShapes(*entry.hitboxActor, desc.teamId, desc.partId, desc.user8);

			entry.physicsHandle = PhysicsHandle{ reinterpret_cast<uint64_t>(entry.controller) };
			entry.mover			= std::make_unique<CharacterMovementComponent>(def->cct.movement, entry.controller, entry.hitboxActor);
			entry.state			= cs;

			SetCharacterState(key, cs);

			m_characterEntries.emplace(key, std::move(entry));
			return m_characterEntries[key].physicsHandle;
		}

		if (!desc.rs.has_value()) return {};
		const auto& rs = desc.rs.value();

		RigidEntry entry{};
		entry.templateHandle = templateHandle;
		entry.actor			 = m_world->CreateActor(templateHandle, ToPhysX(rs.pose), userData);
		if (!entry.actor) return {};

		if (auto* dyn = entry.actor->is<PxRigidDynamic>())
		{
			auto flags = dyn->getRigidBodyFlags();
			flags |= PxRigidBodyFlag::eKINEMATIC;

			dyn->setRigidBodyFlags(flags);
		}

		ApplySpawnPackedIdToActorShapes(*entry.actor, desc.teamId, desc.partId, desc.user8);

		entry.physicsHandle = PhysicsHandle{ reinterpret_cast<uint64_t>(entry.actor) };
		entry.state			= rs;

		SetRigidState(key, rs);

		m_rigidEntries.emplace(key, entry);
		return m_rigidEntries[key].physicsHandle;
	}

	void PhysicsFacade::Despawn(ObjectKey key)
	{
		if (!m_inited.load(std::memory_order_relaxed) || !m_world) return;

		if (auto it = m_rigidEntries.find(key); it != m_rigidEntries.end())
		{
			RigidEntry& entry = it->second;
			if (entry.actor)
			{
				m_world->RemoveActor(entry.actor);
				entry.actor = nullptr;
			}

			m_rigidEntries.erase(it);
		}

		if (auto it = m_characterEntries.find(key); it != m_characterEntries.end())
		{
			CharacterEntry& entry = it->second;

			if (entry.hitboxActor)
			{
				m_world->RemoveActor(entry.hitboxActor);
				entry.hitboxActor = nullptr;
			}

			if (entry.controller)
			{
				m_world->RemoveController(entry.controller);
				entry.controller = nullptr;
			}

			m_characterEntries.erase(it);
		}
	}


	eBodyKind PhysicsFacade::GetKind(ObjectKey key) const
	{
		if (auto it = m_rigidEntries.find(key); it != m_rigidEntries.end())
		{
			const RigidEntry& entry = it->second;
			if (entry.actor)
			{
				if (entry.actor->is<PxRigidStatic>())
					return eBodyKind::RIGID_STATIC;

				if (auto* dyn = entry.actor->is<PxRigidDynamic>())
				{
					if (dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
						return eBodyKind::KINEMATIC;
					return eBodyKind::RIGID_DYNAMIC;
				}
			}
			return eBodyKind::NONE;
		}

		if (auto it = m_characterEntries.find(key); it != m_characterEntries.end())
		{
			if (it->second.controller)
				return eBodyKind::CHARACTER;
		}

		return eBodyKind::NONE;
	}

	eBodyKind PhysicsFacade::GetKind(PrefabKey prefab) const
	{
		switch (PHYSICS_PREFAB_REGISTRY.GetBodyKind(prefab))
		{
		case prefab::ePrefabBodyKind::NONE:
			return eBodyKind::NONE;
		case prefab::ePrefabBodyKind::STATIC:
			return eBodyKind::RIGID_STATIC;
		case prefab::ePrefabBodyKind::DYNAMIC:
			return eBodyKind::RIGID_DYNAMIC;
		case prefab::ePrefabBodyKind::KINEMATIC:
			return eBodyKind::KINEMATIC;
		case prefab::ePrefabBodyKind::CHARACTER:
			return eBodyKind::CHARACTER;
		}

		return eBodyKind::NONE;
	}

	bool PhysicsFacade::GetCharacterState(ObjectKey key, CharacterState& state) const
	{
		auto it = m_characterEntries.find(key);
		if (it == m_characterEntries.end())
			return false;

		const CharacterEntry& e = it->second;
		if (!e.controller || !e.mover)
			return false;

		e.mover->GetCharacterState(state);
		return true;
	}

	bool PhysicsFacade::SetCharacterState(ObjectKey key, const CharacterState& state)
	{
		auto it = m_characterEntries.find(key);
		if (it == m_characterEntries.end())
			return false;

		CharacterEntry& e = it->second;
		if (!e.controller) return false;

		// 1. CCT 위치 복원 + sense 캐시 무효화 (stale grounded 방지)
		e.mover->Teleport(state.pos);

		// 2. CharacterMovementComponent 내부 상태 복원
		if (e.mover)
		{
			MovementState mvState = e.mover->GetMoveState();

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


		return true;
	}

	bool PhysicsFacade::GetRigidState(ObjectKey key, RigidState& state) const
	{
		auto it = m_rigidEntries.find(key);
		if (it == m_rigidEntries.end())
			return false;

		const RigidEntry& e = it->second;
		if (!e.actor)
			return false;

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

	bool PhysicsFacade::SetRigidState(ObjectKey key, const RigidState& state)
	{
		auto it = m_rigidEntries.find(key);
		if (it == m_rigidEntries.end())
			return false;

		RigidEntry& e = it->second;
		if (!e.actor) return false;
		e.state = state;

		e.actor->setGlobalPose(ToPhysX(state.pose));

		if (auto* dyn = e.actor->is<PxRigidDynamic>())
		{
			dyn->setLinearVelocity(ToPhysX(state.linVel));
			dyn->setAngularVelocity(ToPhysX(state.angVel));
		}

		return true;
	}

	void PhysicsFacade::ApplyCharacterInput(ObjectKey key, const CharacterInput& input)
	{
		auto it = m_characterEntries.find(key);
		if (it == m_characterEntries.end()) return;

		CharacterEntry& entry = it->second;
		if (!entry.controller) return;

		entry.lastIntent		= ToMoveIntent(input);
		entry.state.facingYaw	= input.facingYaw;
		entry.state.facingPitch = input.facingPitch;
	}

	bool PhysicsFacade::RaycastLos(const Vec3& from, const Vec3& to) const
	{
		if (!m_world) return true;
		PxScene* scene = m_world->GetScene();
		if (!scene) return true;

		const PxVec3 origin = ToPhysX(from);
		PxVec3 dir = ToPhysX(to - from);
		const float  dist = dir.magnitude();
		if (dist < 1e-3f) return true;
		dir /= dist;

		// WORLD 카테고리만 검사, sublayer=1 → ShapeQuery::NO_LOS_BLOCK 존중
		PxFilterData qfd{};
		qfd.word0 = QueryCategory::Flags(QueryCategory::WORLD).bits();
		qfd.word1 = QueryMeta::Make(/*channel*/ 0, /*sublayer*/ 1).v;

		QueryFilterCallbackT<> cb{};
		cb.map.world = PxQueryHitType::eBLOCK;
		cb.map.character = PxQueryHitType::eNONE;
		cb.map.hitbox = PxQueryHitType::eNONE;
		cb.map.trigger = PxQueryHitType::eNONE;

		const PxQueryFilterData fd(qfd, PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);

		PxRaycastBuffer buf;
		return !scene->raycast(origin, dir, dist, buf, PxHitFlag::eDEFAULT, fd, &cb);
	}

	void PhysicsFacade::MoveCharacter(float dt)
	{
		for (auto& entry : m_characterEntries | views::values)
		{
			if (!entry.controller || !entry.mover || entry.isKinematic) continue;

			entry.mover->Tick(dt, entry.lastIntent);
		}
	}
}
