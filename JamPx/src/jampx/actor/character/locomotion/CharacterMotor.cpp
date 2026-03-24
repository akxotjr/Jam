#include "pch.h"
#include "jampx/actor/character/locomotion/CharacterMotor.h"

//#include <iostream>

#include "jampx/actor/character/locomotion/CharacterFilter.h"

namespace jam::px
{
	namespace
	{
		inline MoveCollision::Flags ToFlags(const physx::PxControllerCollisionFlags& f)
		{
			MoveCollision::Flags out;
			if (f & PxControllerCollisionFlag::eCOLLISION_SIDES) out.set(MoveCollision::SIDES);
			if (f & PxControllerCollisionFlag::eCOLLISION_UP)	out.set(MoveCollision::UP);
			if (f & PxControllerCollisionFlag::eCOLLISION_DOWN)	out.set(MoveCollision::DOWN);
			return out;
		}


	}

	CharacterMotor::CharacterMotor(PxCapsuleController* controller, PxRigidActor* hitbox)
		: m_controller(controller)
	{
		if (!hitbox) return;

		if (auto* dyn = hitbox->is<PxRigidDynamic>())
		{
			// hitbox는 follow 대상이므로 kinematic만 허용
			if (dyn->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
				m_hitbox = dyn;
		}

		m_rqfd = MakeRequestQueryFD(
			QueryCategory::WORLD, 
			0, 0, 0, 
			0, 0, 0, 
			RequestQueryFlag::MAP_DEFAULT
			| RequestQueryFlag::IGNORE_SELF_ACTOR
			| RequestQueryFlag::IGNORE_TRIGGERS);

		DefaultQueryPolicy qryPolicy;

		if (m_controller)
			qryPolicy.selfActor = m_controller->getActor();

		m_qryCallback = std::make_unique<QueryFilterCallbackT<>>(qryPolicy, QueryHitTypeMap{});
		m_cctCallback = std::make_unique<CharacterFilterCallbackT<>>(DefaultCharacterFilterPolicy{});

	}

	CharacterMotor::~CharacterMotor()
	{
		m_controller = nullptr;
		m_hitbox	 = nullptr;
	}

	MotorSense CharacterMotor::Sense() const
	{
		if (!m_lastSenseValid) RefreshSenseCache();
		return m_lastSense;
	}

	bool CharacterMotor::TryResize(float height)
	{
		if (!m_controller) return false;

		m_controller->setHeight(height);
		m_lastSenseValid = false;

		FollowHitboxToController();
		return true;
	}

	MotorStepResult CharacterMotor::Move(float dt, const Vec3& displacement)
	{
		MotorStepResult r{};
		if (!m_controller) return r;

		dt = std::max(dt, 0.0f);

		const PxExtendedVec3& before = m_controller->getPosition();
		const PxVec3 disp = ToPhysX(displacement);

		PxFilterData fd = m_rqfd.ToPx();
		PxControllerFilters filters(&fd, m_qryCallback.get(), m_cctCallback.get());

		const PxControllerCollisionFlags flags = m_controller->move(disp, EPSILON, dt, filters);
		const PxExtendedVec3& after = m_controller->getPosition();

		//// ---- DEBUG LOG BEGIN ----
		//{
		//	const bool hitSide = flags.isSet(PxControllerCollisionFlag::eCOLLISION_SIDES);
		//	const bool hitUp = flags.isSet(PxControllerCollisionFlag::eCOLLISION_UP);
		//	const bool hitDown = flags.isSet(PxControllerCollisionFlag::eCOLLISION_DOWN);

		//	const auto d = diff(after, before);

		//	std::cout
		//		<< "[CCT_MOVE] "
		//		<< "dt=" << dt
		//		<< " disp=(" << disp.x << "," << disp.y << "," << disp.z << ")"
		//		<< " before=(" << before.x << "," << before.y << "," << before.z << ")"
		//		<< " after=(" << after.x << "," << after.y << "," << after.z << ")"
		//		<< " delta=(" << d.x << "," << d.y << "," << d.z << ")"
		//		<< " flags[s=" << (hitSide ? 1 : 0)
		//		<< ",u=" << (hitUp ? 1 : 0)
		//		<< ",d=" << (hitDown ? 1 : 0) << "]"
		//		<< "\n";
		//}
		//// ---- DEBUG LOG END ----

		auto delta = diff(after, before);

		r.position = ToPx(after);
		r.velocity = ToPx(delta) * (1.0f / dt);

		m_lastSense.grounded		= flags.isSet(PxControllerCollisionFlag::eCOLLISION_DOWN);
		m_lastSense.ceiling			= flags.isSet(PxControllerCollisionFlag::eCOLLISION_UP);
		m_lastSense.groundNormal = { 0,1,0 };

		r.sense = m_lastSense;

		m_lastSenseValid = true;

		FollowHitboxToController();

		return r;
	}

	Vec3 CharacterMotor::GetPosition() const
	{
		if (!m_controller) return {};
		return ToPx(m_controller->getPosition());
	}

	float CharacterMotor::GetRadius() const
	{
		if (!m_controller) return 0.0f;
		return m_controller->getRadius();
	}

	float CharacterMotor::GetHeight() const
	{
		if (!m_controller) return 0.0f;
		return m_controller->getHeight();
	}

	void CharacterMotor::Teleport(const Vec3& p)
	{
		if (!m_controller) return;

		m_controller->setPosition(ToPhysXEx(p));
		m_lastSenseValid = false;

		FollowHitboxToController();
	}

	void CharacterMotor::OverrideSense(bool grounded, bool ceiling)
	{
		m_lastSense.grounded = grounded;
		m_lastSense.ceiling  = ceiling;
		m_lastSenseValid	 = true;   // 다음 Sense()가 getState() 대신 이 값을 사용
	}

	void CharacterMotor::RefreshSenseCache() const
	{
		if (!m_controller) return;

		PxControllerState st{};
		m_controller->getState(st);

		const PxControllerCollisionFlags flags(static_cast<PxU8>(st.collisionFlags));

		m_lastSense.grounded		= (st.collisionFlags & PxControllerCollisionFlag::eCOLLISION_DOWN);
		m_lastSense.ceiling			= (st.collisionFlags & PxControllerCollisionFlag::eCOLLISION_UP);
		m_lastSense.collisionFlags	= ToFlags(flags);
		m_lastSense.groundNormal = { 0,1,0 };

		m_lastSenseValid = true;
	}

	void CharacterMotor::FollowHitboxToController()
	{
		if (!m_controller || !m_hitbox) return;

		const PxExtendedVec3& p = m_controller->getPosition();
		const PxTransform tf(toVec3(p));

		m_hitbox->setKinematicTarget(tf);
	}
}
