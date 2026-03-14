#pragma once

#include "jampx/PhysicsTypes.h"
#include "jampx/actor/rigid/IRigidBehavior.h"
#include "jampx/actor/rigid/kinematic/IKinematicDriver.h"

namespace jam::px
{
	class KinematicRigidBehavior : public IRigidBehavior
	{
	public:
		explicit KinematicRigidBehavior(std::unique_ptr<IKinematicDriver> driver);
		~KinematicRigidBehavior() override = default;


		void			Tick(RigidBody& body, float dt) override;
		void			SyncState(RigidBody& body) override;
		eActorType		GetActorType() const override { return eActorType::Generic; }

		bool			ApplyAuthoritativeState(const RigidState& s) override;

	private:

		std::unique_ptr<IKinematicDriver>	m_driver = nullptr;
		float								m_lastDt = 0.0f;
	};


} // namespace jam::px
