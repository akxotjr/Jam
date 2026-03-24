#include "pch.h"
#include "jampx/actor/rigid/kinematic/KinematicRigidBehavior.h"
#include "jampx/actor/rigid/RigidBody.h"
#include "jampx/actor/rigid/kinematic/KinematicDrivers.h"

namespace jam::px
{
	KinematicRigidBehavior::KinematicRigidBehavior(
		std::unique_ptr<IKinematicDriver> mainDriver,
		std::unique_ptr<IKinematicDriver> replayDriver)
		: m_mainDriver(std::move(mainDriver)), m_replayDriver(std::move(replayDriver))
	{
	}

	void KinematicRigidBehavior::TickOnMain(RigidBody& body, float dt)
	{
        if (!m_mainDriver || m_mainDriver->IsDone())
            return;

		auto* dyn = body.GetMainActor()->is<PxRigidDynamic>();
		if (!dyn) return;

		m_lastDtMain = dt;
		const PxTransform pose = m_mainDriver->Tick(dt);
		dyn->setKinematicTarget(pose);
	}

	void KinematicRigidBehavior::TickOnReplay(RigidBody& body, float dt)
	{
		if (!m_replayDriver)
			return;

		auto* dyn = body.GetReplayActor()->is<PxRigidDynamic>();
		if (!dyn) return;

		m_lastDtReplay = dt;
		const PxTransform pose = m_replayDriver->Tick(dt);
		dyn->setKinematicTarget(pose);
	}

	bool KinematicRigidBehavior::SyncMainState(RigidBody& body)
	{
		auto* dyn = body.GetMainActor()->is<PxRigidDynamic>();
		if (!dyn) return false;

		const RigidState prev = body.GetMainState();

		const PxTransform prevPose = ToPhysX(prev.pose);
		const PxTransform nowPose = dyn->getGlobalPose();

		RigidState now = {
			.pose	  = ToPx(nowPose),
			.linVel	  = ToPx(GetLinearVelocity(nowPose.p, prevPose.p, m_lastDtMain)),
			.angVel	  = ToPx(GetAngularVelocity(nowPose.q, prevPose.q, m_lastDtMain)),
			.kineType = prev.kineType
		};

		bool changed = !IsNearlyEqual(prevPose, nowPose);

		if (prev.kineType != eKineDrivenType::RuntimeDynamic)
		{
			const auto newKineState = m_mainDriver->BuildState();
			const bool kineStateChanged = prev.kineState != newKineState;
			now.kineState = newKineState;
			changed = changed || kineStateChanged;
		}

		body.SetMainState(now);
		return changed;
	}

	void KinematicRigidBehavior::SyncReplayState(RigidBody& body)
	{
		auto* dyn = body.GetReplayActor()->is<PxRigidDynamic>();
		if (!dyn) return;

		const RigidState prev = body.GetReplayState();
		
		const PxTransform prevPose = ToPhysX(prev.pose);
		const PxTransform nowPose  = dyn->getGlobalPose();
		
		RigidState now = {
			.pose     = ToPx(nowPose),
			.linVel   = ToPx(GetLinearVelocity(nowPose.p, prevPose.p, m_lastDtReplay)),
			.angVel   = ToPx(GetAngularVelocity(nowPose.q, prevPose.q, m_lastDtReplay)),
			.kineType = prev.kineType
		};

		if (IsLocalDrivenKine(now.kineType))
		{
			now.kineState = m_replayDriver->BuildState();
		}

		body.SetReplayState(now);
	}

	bool KinematicRigidBehavior::ApplyMainState(const RigidState& state)
	{
		auto* net = dynamic_cast<NetworkPoseKinematicDriver*>(m_mainDriver.get());
		if (!net) return false;

		net->SetAuthoritativePose(ToPhysX(state.pose));
		net->SetAuthoritativeLinearVelocity(ToPhysX(state.linVel));
		net->SetAuthoritativeAngularVelocity(ToPhysX(state.angVel));

		return true;
	}

	bool KinematicRigidBehavior::ApplyReplayState(const RigidState& state)
	{
		auto* netDriver = dynamic_cast<NetworkPoseKinematicDriver*>(m_replayDriver.get());
		if (!netDriver) return false;

		netDriver->SetAuthoritativePose(ToPhysX(state.pose));
		netDriver->SetAuthoritativeLinearVelocity(ToPhysX(state.linVel));
		netDriver->SetAuthoritativeAngularVelocity(ToPhysX(state.angVel));

		return true;
	}


} // namespace jam::px

