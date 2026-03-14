#include "pch.h"
#include "jampx/PhysicsWorld.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/actor/character/locomotion/CharacterFilter.h"

namespace jam::px
{
	namespace
	{
		//class CharacterControllerHitReport final : public PxUserControllerHitReport
		//{
		//public:
		//	void onShapeHit(const PxControllerShapeHit& hit) override
		//	{
		//		PxRigidActor* actor = hit.actor;
		//		if (!actor) return;

		//		PxRigidDynamic* dyn = actor->is<PxRigidDynamic>();
		//		if (!dyn) return;

		//		if (dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
		//			return;

		//		// 아주 약하게 밀리는 정도
		//		const PxVec3 dir = hit.dir;
		//		if (dir.magnitudeSquared() <= 1e-6f)
		//			return;

		//		const PxVec3 n = dir.getNormalized();

		//		// impulse 크기(원하는 느낌에 따라 조절)
		//		constexpr float kPushImpulse = 0.25f;
		//		dyn->addForce(n * kPushImpulse, PxForceMode::eIMPULSE, true);
		//	}

		//	void onControllerHit(const PxControllersHit&) override {}
		//	void onObstacleHit(const PxControllerObstacleHit&) override {}
		//};

		//CharacterControllerHitReport g_characterControllerHitReport;


	}


	void PhysicsWorld::Init(ShardPxCpuDispacter* dispacter)
	{
		if (m_pxScene) return;

		if (dispacter)
		{
			auto desc = PhysicsCore::Instance().MakeDefaultSceneDesc();
			desc.cpuDispatcher = dispacter;
			m_pxScene = PhysicsCore::Instance().CreateScene(desc);
		}
		else
		{
			m_pxScene = PhysicsCore::Instance().CreateScene();
		}

		m_simCallback = std::make_unique<SimulationEventCallback>();
		m_pxScene->setSimulationEventCallback(m_simCallback.get());
		m_controllerManager = PxCreateControllerManager(*m_pxScene, false);

		m_characterBehaviorCB = new CharacterBehaviorCallbackT<>(DefaultCharacterBehaviorPolicy{});
		m_characterReportCB   = new CharacterHitReportT<>(DefaultCharacterHitReportPolicy{});
	}

	void PhysicsWorld::Destroy()
	{
		if (m_controllerManager) { m_controllerManager->release(); m_controllerManager = nullptr; }
		if (m_pxScene)           { m_pxScene->release();           m_pxScene = nullptr; }
	}

	void PhysicsWorld::Simulate(float elapsed) const
	{
		if (!m_pxScene) return;

		m_pxScene->simulate(elapsed);
		m_pxScene->fetchResults(true);
	}

	void PhysicsWorld::BeginSimulate(float elapsed, PxBaseTask* completionTask) const
	{
		if (!m_pxScene) return;
		m_pxScene->simulate(elapsed, completionTask);
	}

	void PhysicsWorld::EndSimulate() const
	{
		if (!m_pxScene) return;
		m_pxScene->fetchResults(true); // Completion Task 이후이므로 즉시 반환됨
	}


	PxRigidActor* PhysicsWorld::CreateRigidActor(TemplateHandle tpl, const PxTransform& pose, void* userData)
	{
		if (!m_pxScene)
			return nullptr;

		if (!PHYSICS_PREFAB_REGISTRY.HasTemplate(tpl))
			return nullptr;

		PxRigidActor* inst = PHYSICS_PREFAB_REGISTRY.Instantiate(tpl, pose, userData);
		if (!inst) return nullptr;

		JAM_ASSERT(m_pxScene->addActor(*inst))

		return inst;
	}

	void PhysicsWorld::RemoveRigidActor(PxRigidActor* actor) const
	{
		if (!actor || !m_pxScene) return;

		m_pxScene->removeActor(*actor);
		actor->release();
	}

	PxCapsuleController* PhysicsWorld::CreateController(const CCTBodyDef& def, const PxVec3& pos, void* userData)
	{
		if (!m_controllerManager) return nullptr;

		PxCapsuleControllerDesc desc{};
		desc.upDirection			= PxVec3(0.f, 1.0f, 0.f);
		desc.position				= PxExtendedVec3(pos.x, pos.y, pos.z);
		desc.radius					= def.radius;
		desc.height					= def.height;
		desc.material				= JAM_PX_MATERIAL(def.material);
		desc.density				= def.density;
		desc.userData				= userData;

		desc.slopeLimit				= def.slopeLimit;
		desc.invisibleWallHeight	= def.invisibleWallHeight;
		desc.maxJumpHeight			= def.maxJumpHeight;
		desc.contactOffset			= def.contactOffset;
		desc.stepOffset				= def.stepOffset;
		desc.scaleCoeff				= def.scaleCoeff;
		desc.volumeGrowth			= def.volumeGrowth;

		if (!desc.isValid()) return nullptr;

		auto* cct = m_controllerManager->createController(desc);
		if (auto cctType = cct->getType(); cctType != physx::PxControllerShapeType::eCAPSULE)
			return nullptr;

		return static_cast<PxCapsuleController*>(cct);
	}

	PxRigidDynamic* PhysicsWorld::CreateHitbox(const std::vector<ShapeHandle>& shapeHandles, const PxVec3& pos, void* userData)
	{
		std::vector<PxShape*> shapes;

		JAM_PX_SHAPES(shapeHandles, shapes);

		PxRigidDynamic* hitbox = PX_PHYSICS->createRigidDynamic(PxTransform(pos));
		if (!hitbox) return nullptr;

		hitbox->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

		for (auto shape : shapes)
		{
			hitbox->attachShape(*shape);
		}
		m_pxScene->addActor(*hitbox);

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
		if (!m_simCallback) return{};
		return m_simCallback->ConsumeEvents();
	}

	std::vector<ObjectId> PhysicsWorld::ConsumeAdvancdActive()
	{
		if (!m_simCallback) return {};
		return m_simCallback->ConsumeActiveList();
	}
}
