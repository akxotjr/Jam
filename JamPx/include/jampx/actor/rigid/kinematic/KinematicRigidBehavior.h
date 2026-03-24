#pragma once

#include "jampx/PhysicsTypes.h"
#include "jampx/actor/rigid/IRigidBehavior.h"
#include "jampx/actor/rigid/kinematic/IKinematicDriver.h"

namespace jam::px
{
	class KinematicRigidBehavior : public IRigidBehavior
	{
	public:
		explicit KinematicRigidBehavior(
			std::unique_ptr<IKinematicDriver> mainDriver, 
			std::unique_ptr<IKinematicDriver> replayDriver);
		~KinematicRigidBehavior() override = default;


		void			TickOnMain(RigidBody& body, float dt) override;
		void			TickOnReplay(RigidBody& body, float dt) override;

		bool			SyncMainState(RigidBody& body) override;
		void			SyncReplayState(RigidBody& body) override;
		
		eActorType		GetActorType() const override { return eActorType::Generic; }

		bool			ApplyMainState(const RigidState& state) override;
		bool			ApplyReplayState(const RigidState& state) override;

	private:
		std::unique_ptr<IKinematicDriver>	m_mainDriver	= nullptr;
		std::unique_ptr<IKinematicDriver>	m_replayDriver	= nullptr;

		float								m_lastDtMain	= 0.0f;
		float								m_lastDtReplay	= 0.f;
	};


} // namespace jam::px
