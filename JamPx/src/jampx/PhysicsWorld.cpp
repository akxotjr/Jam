#include "pch.h"
#include "jampx/PhysicsWorld.h"

#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/prefab/PrefabAssetCreator.h"

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


		inline PxRigidActor* CreateCharacterHitboxActor(
			PxScene& scene,
			prefab::TemplateHandle handle,
			const PxTransform& worldPose,
			void* userData)
		{
			const prefab::PrefabTemplateDef* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(handle);
			if (!def) return nullptr;

			if (def->kind != eBodyKind::CHARACTER)
				return nullptr;

			if (!def->cct.hasHitbox)
				return nullptr;

			if (def->shapes.empty())
				return nullptr;

			std::vector<PxShape*> shapes;
			PHYSICS_PREFAB_REGISTRY.GetShapes(def->shapes, shapes);

			// Kinematic actor로 생성해서 follow할 대상
			prefab::PrefabTemplateDef hitboxDef{};
			hitboxDef.kind = eBodyKind::KINEMATIC;

			PxRigidActor* actor = prefab::PrefabAssetCreator::CreateRigidActor(hitboxDef, shapes);
			if (!actor) return nullptr;

			actor->setGlobalPose(worldPose);
			actor->userData = userData;

			scene.addActor(*actor);
			return actor;
		}
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


	PxRigidActor* PhysicsWorld::CreateActor(TemplateHandle templateHandle, const PxTransform& worldPose, void* userData)
	{
		if (!m_pxScene)
			return nullptr;

		if (!PHYSICS_PREFAB_REGISTRY.HasTemplate(templateHandle))
			return nullptr;

		PxRigidActor* inst = PHYSICS_PREFAB_REGISTRY.Instantiate(templateHandle, worldPose, userData);
		if (!inst) return nullptr;

		JAM_ASSERT(m_pxScene->addActor(*inst))

		return inst;
	}

	void PhysicsWorld::RemoveActor(PxRigidActor* actor) const
	{
		if (!actor || !m_pxScene) return;

		m_pxScene->removeActor(*actor);
		actor->release();
	}


	PxCapsuleController* PhysicsWorld::CreateController(TemplateHandle handle, const PxVec3& pos, void* userData, CharacterMoveConfig& outMoveCfg)
	{
		if (!m_controllerManager) return nullptr;

		const ActorTemplateDef* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(handle);
		if (!def) return nullptr;

		if (def->kind != eBodyKind::CHARACTER)
			return nullptr;

		const prefab::PrefabCCTDef& cct = def->cct;

		outMoveCfg = cct.movement;

		PxCapsuleControllerDesc desc{};
		desc.upDirection		= PxVec3(0.f, 1.f, 0.f);
		desc.position			= PxExtendedVec3(pos.x, pos.y, pos.z);
		desc.radius				= cct.radius;
		desc.height				= cct.height;
		desc.material			= PHYSICS_PREFAB_REGISTRY.GetMaterial(cct.material);
		desc.contactOffset		= cct.contactOffset;
		desc.stepOffset			= cct.stepOffset;
		desc.slopeLimit			= cct.slopeLimit;
		desc.reportCallback		= m_characterReportCB;
		desc.behaviorCallback	= m_characterBehaviorCB;
		desc.userData			= userData;
		

		if (!desc.isValid())
			return nullptr;


		return static_cast<PxCapsuleController*>(m_controllerManager->createController(desc));
	}

	void PhysicsWorld::RemoveController(PxController* controller)
	{
		if (!controller)
			return;

		controller->release();
	}

	PhysicsWorld::CreatedCharacter PhysicsWorld::CreateCharacter(TemplateHandle handle, const PxVec3& pos, void* userData)
	{
		CreatedCharacter out{};

		if (!m_pxScene || !m_controllerManager) return out;

		const prefab::PrefabTemplateDef* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(handle);
		if (!def) return out;

		if (def->kind != eBodyKind::CHARACTER)
			return out;

		const prefab::PrefabCCTDef& cct = def->cct;
		out.moveCfg = cct.movement;

		PxCapsuleControllerDesc desc{};
		desc.upDirection = PxVec3(0.f, 1.f, 0.f);
		desc.position = PxExtendedVec3(pos.x, pos.y, pos.z);
		desc.radius = cct.radius;
		desc.height = cct.height;
		desc.material = PHYSICS_PREFAB_REGISTRY.GetMaterial(cct.material);
		desc.contactOffset = cct.contactOffset;
		desc.stepOffset = cct.stepOffset;
		desc.slopeLimit = cct.slopeLimit;
		desc.reportCallback = m_characterReportCB;
		desc.behaviorCallback = m_characterBehaviorCB;
		desc.userData = userData;


		if (!desc.isValid())
			return out;

		out.controller = static_cast<PxCapsuleController*>(m_controllerManager->createController(desc));
		if (!out.controller)
			return out;

		const PxTransform tf(PxVec3(pos.x, pos.y, pos.z));
		out.hitboxActor = CreateCharacterHitboxActor(*m_pxScene, handle, tf, userData);

		return out;
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
