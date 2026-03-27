#include "pch.h"
#include "jampx/actor/rigid/kinematic/KinematicRigidBehavior.h"
#include "jampx/actor/rigid/RigidBody.h"
#include "jampx/actor/rigid/kinematic/KinematicDrivers.h"

namespace jam::px
{
	namespace 
	{
		bool IsMeaningFulChanged(const RigidState& prev, const RigidState& now, bool isLocalDriven)
		{
			if (isLocalDriven)
			{
				return (prev.kineState.startEpoch != now.kineState.startEpoch)
					|| (prev.kineState.phase	  != now.kineState.phase)
					|| (prev.kineState.targetId   != now.kineState.targetId)
					|| (prev.kineState.eventMask  != now.kineState.eventMask);
			}
			
			if ((prev.pose.p - now.pose.p).MagnitudeSquared() > (EPS_3 * EPS_3))						 return true;
			if (std::fabs(prev.pose.q.GetNormalized().Dot(now.pose.q.GetNormalized())) < (1.0f - EPS_4)) return true;
			if ((prev.linVel - now.linVel).MagnitudeSquared() > (EPS_2 * EPS_2))						 return true;
			if ((prev.angVel - now.angVel).MagnitudeSquared() > (EPS_2 * EPS_2))						 return true;
			
			return false;
		}

	} // anonymous namespace


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
		const auto* dyn = body.GetMainActor()->is<PxRigidDynamic>();
		if (!dyn) return false;

		const RigidState prev = body.GetMainState();

		const PxTransform prevPose = ToPhysX(prev.pose);
		const PxTransform nowPose  = dyn->getGlobalPose();

		RigidState now = {
			.pose	  = ToPx(nowPose),
			.linVel	  = ToPx(GetLinearVelocity(nowPose.p, prevPose.p, m_lastDtMain)),
			.angVel	  = ToPx(GetAngularVelocity(nowPose.q, prevPose.q, m_lastDtMain)),
			.kineType = prev.kineType
		};

		bool changed = false;
		if (IsLocalDrivenKine(prev.kineType))
		{
			now.kineState = m_mainDriver ? m_mainDriver->BuildState() : prev.kineState;
			changed = IsMeaningFulChanged(prev, now, true);
		}
		else
		{
			changed = IsMeaningFulChanged(prev, now, false);
		}

		body.SetMainState(now);
		return changed;
	}

	void KinematicRigidBehavior::SyncReplayState(RigidBody& body)
	{
		const auto* dyn = body.GetReplayActor()->is<PxRigidDynamic>();
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
		if (auto* netDriver = dynamic_cast<NetworkPoseKinematicDriver*>(m_replayDriver.get()))
		{
			netDriver->SetAuthoritativePose(ToPhysX(state.pose));
			netDriver->SetAuthoritativeLinearVelocity(ToPhysX(state.linVel));
			netDriver->SetAuthoritativeAngularVelocity(ToPhysX(state.angVel));
			return true;
		}

		if (state.kineType == eKineDrivenType::TargetDerived)
		{
			const ObjectId target = state.kineState.targetId;
			if (target != INVALID_OBJ_ID && m_mainDriver)
				m_mainDriver->SetTargetId(target);
		}

		return false;
	}

	bool KinematicRigidBehavior::ApplyReplayState(const RigidState& state)
	{
		auto* netDriver = dynamic_cast<NetworkPoseKinematicDriver*>(m_replayDriver.get());
		if (!netDriver) return false;

		netDriver->SetAuthoritativePose(ToPhysX(state.pose));
		netDriver->SetAuthoritativeLinearVelocity(ToPhysX(state.linVel));
		netDriver->SetAuthoritativeAngularVelocity(ToPhysX(state.angVel));


		if (state.kineType == eKineDrivenType::TargetDerived)
		{
			const ObjectId target = state.kineState.targetId;
			if (target != INVALID_OBJ_ID && m_replayDriver)
				m_replayDriver->SetTargetId(target);
		}

		return true;
	}


} // namespace jam::px

