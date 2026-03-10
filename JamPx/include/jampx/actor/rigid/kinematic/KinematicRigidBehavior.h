#pragma once

#include "jampx/api/PhysicsTypes.h"
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
		eActorType		GetActorType() const override { return eActorType::Generic; }

	private:

		std::unique_ptr<IKinematicDriver> m_driver = nullptr;
	};


} // namespace jam::px
