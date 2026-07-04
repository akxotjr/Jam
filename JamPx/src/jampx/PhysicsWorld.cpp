#include "pch.h"
#include "jampx/PhysicsWorld.h"
#include "jampx/prefab/PhysicsArchetypeRegistry.h"
#include "jampx/actor/character/locomotion/CharacterFilter.h"

namespace jam::px
{
	void PhysicsWorld::Init(PhysicsArchetypeRegistry* registry, ShardPxCpuDispacter* dispacter)
	{
		if (m_scenes[0]) return;
		m_registry = registry;

		for (int i = 0; i < 2; ++i)
		{
			if (dispacter)
			{
				auto desc = PhysicsCore::Instance().MakeDefaultSceneDesc();
				desc.cpuDispatcher = dispacter;
				m_scenes[i] = PhysicsCore::Instance().CreateScene(desc);
			}
			else
			{
				m_scenes[i] = PhysicsCore::Instance().CreateScene();
			}

			m_simCallbacks[i] = std::make_unique<SimulationEventCallback>();
			m_scenes[i]->setSimulationEventCallback(m_simCallbacks[i].get());
			m_controllerMgrs[i] = PxCreateControllerManager(*m_scenes[i], false);
		}

		m_characterBehaviorCB = new CharacterBehaviorCallbackT<>(DefaultCharacterBehaviorPolicy{});
		m_characterReportCB   = new CharacterHitReportT<>(DefaultCharacterHitReportPolicy{});
	}

	void PhysicsWorld::Destroy()
	{
		for (int i = 0; i < 2; ++i)
		{
			if (m_controllerMgrs[i]) { m_controllerMgrs[i]->release(); m_controllerMgrs[i] = nullptr; }
			if (m_scenes[i])         { m_scenes[i]->release();         m_scenes[i] = nullptr; }
		}
		m_registry = nullptr;
	}

	PhysicsArchetypeRegistry& PhysicsWorld::Registry() const
	{
		JAM_ASSERT(m_registry);
		return *m_registry;
	}

	PxScene* PhysicsWorld::GetScene(ePxSceneSlot slot) const
	{
		const int32 idx = E2U(slot);
		return (idx >= 0 && idx < 2) ? m_scenes[idx] : nullptr;
	}

	void PhysicsWorld::Simulate(ePxSceneSlot slot, float elapsed) const
	{
		PxScene* scene = GetScene(slot);
		if (!scene) return;

		scene->simulate(elapsed);
		scene->fetchResults(true);
	}

	void PhysicsWorld::BeginSimulate(ePxSceneSlot slot, float elapsed, PxBaseTask* completionTask) const
	{
		PxScene* scene = GetScene(slot);
		if (!scene) return;
		scene->simulate(elapsed, completionTask);
	}

	void PhysicsWorld::EndSimulate(ePxSceneSlot slot) const
	{
		PxScene* scene = GetScene(slot);
		if (!scene) return;
		scene->fetchResults(true);
	}

	PxRigidActor* PhysicsWorld::CreateRigidActor(ePxSceneSlot slot, PhysicsArchetypeKey tpl, const PxTransform& pose, void* userData)
	{
		PxScene* scene = GetScene(slot);
		if (!scene) return nullptr;

		if (!m_registry || !m_registry->HasArchetype(tpl))
			return nullptr;

		PxRigidActor* inst = m_registry->Instantiate(tpl, pose, userData);
		if (!inst) return nullptr;

		JAM_VERIFY(scene->addActor(*inst));

		return inst;
	}

	void PhysicsWorld::RemoveRigidActor(ePxSceneSlot slot, PxRigidActor* actor) const
	{
		if (!actor) return;

		PxScene* scene = GetScene(slot);
		if (!scene) return;

		scene->removeActor(*actor);
		actor->release();
	}

	PxCapsuleController* PhysicsWorld::CreateController(ePxSceneSlot slot, const CCTBodyData& data, const PxVec3& pos, void* userData)
	{
		const int32 idx = E2U(slot);
		PxControllerManager* mgr = (idx >= 0 && idx < 2) ? m_controllerMgrs[idx] : nullptr;
		if (!mgr) return nullptr;

		PxCapsuleControllerDesc desc{};
		desc.upDirection			= PxVec3(0.f, 1.0f, 0.f);
		desc.position				= PxExtendedVec3(pos.x, pos.y, pos.z);
		desc.radius					= data.radius;
		desc.height					= data.height;
		desc.material				= Registry().GetMaterial(data.material);
		desc.density				= data.density;
		desc.userData				= userData;

		desc.slopeLimit				= data.slopeLimit;
		desc.invisibleWallHeight	= data.invisibleWallHeight;
		desc.maxJumpHeight			= data.maxJumpHeight;
		desc.contactOffset			= data.contactOffset;
		desc.stepOffset				= data.stepOffset;
		desc.scaleCoeff				= data.scaleCoeff;
		desc.volumeGrowth			= data.volumeGrowth;

		if (!desc.isValid()) return nullptr;

		auto* cct = mgr->createController(desc);
		if (auto cctType = cct->getType(); cctType != physx::PxControllerShapeType::eCAPSULE)
			return nullptr;

		return static_cast<PxCapsuleController*>(cct);
	}

	PxRigidDynamic* PhysicsWorld::CreateHitbox(const std::vector<ShapeHandle>& shapeHandles, const PxVec3& pos, void* userData)
	{
		// Hitbox는 Main Scene에만 생성 (쿼리 전용이므로 replay 불필요)
		PxScene* scene = GetScene(ePxSceneSlot::Main);
		if (!scene) return nullptr;

		PxRigidDynamic* hitbox = PX_PHYSICS->createRigidDynamic(PxTransform(pos));
		if (!hitbox) return nullptr;

		hitbox->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
		hitbox->userData = userData;

		for (auto h : shapeHandles)
		{
			PxShape* cached = Registry().GetShape(h);
			if (!cached) continue;

			PxShape* shape = physx::PxCloneShape(*PX_PHYSICS, *cached, true);
			if (!shape)
			{
				hitbox->release();
				return nullptr;
			}

			shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
			shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
			shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);

			PxFilterData qfd = shape->getQueryFilterData();
			qfd.word0 = QueryCategory::HITBOX;
			shape->setQueryFilterData(qfd);

			hitbox->attachShape(*shape);
			shape->release();
		}

		scene->addActor(*hitbox);

		return hitbox;
	}

	void PhysicsWorld::RemoveController(PxController* controller)
	{
		if (!controller)
			return;

		controller->release();
	}

	std::vector<SimEvent> PhysicsWorld::ConsumeSimEvents()
	{
		// Main scene 이벤트만 반환 (gameplay relevant)
		if (!m_simCallbacks[0]) return{};
		return m_simCallbacks[0]->ConsumeEvents();
	}

	std::vector<ObjectId> PhysicsWorld::ConsumeAdvancdActive()
	{
		// Main scene active list만 반환
		if (!m_simCallbacks[0]) return {};
		return m_simCallbacks[0]->ConsumeActiveList();
	}
}
