#include "pch.h"
#include "jampx/actor/rigid/RigidBody.h"
#include "jampx/actor/rigid/kinematic/KinematicRigidBehavior.h"


namespace jam::px
{
	namespace 
	{
		void ApplyStateToActor(PxRigidActor* actor, const RigidState& s, bool kinematicLike)
		{
			if (!actor) return;

			if (auto* dyn = actor->is<PxRigidDynamic>())
			{
				if (kinematicLike)
				{
					dyn->setKinematicTarget(ToPhysX(s.pose));
				}
				else
				{
					dyn->setGlobalPose(ToPhysX(s.pose));
					dyn->setLinearVelocity(ToPhysX(s.linVel));
					dyn->setAngularVelocity(ToPhysX(s.angVel));
				}
				return;
			}

			if (auto* st = actor->is<PxRigidStatic>())
			{
				st->setGlobalPose(ToPhysX(s.pose));
			}
		}

	} // anonymous namespace


	RigidBody::RigidBody(PxRigidActor* actor, PxRigidActor* replayActor, std::unique_ptr<IRigidBehavior> behavior)
		: m_mainActor(actor), m_replayActor(replayActor), m_behavior(std::move(behavior))
	{
	}

	void RigidBody::TickOnMain(float dt)
	{
		if (!m_behavior) return;
		m_behavior->TickOnMain(*this, dt);
	}

	void RigidBody::TickOnReplay(float dt)
	{
		if (!m_behavior) return;
		m_behavior->TickOnReplay(*this, dt);
	}

	bool RigidBody::SyncMainState(RigidBody& body)
	{
		if (!m_behavior) return false;
		return m_behavior->SyncMainState(body);
	}

	void RigidBody::SyncReplayState(RigidBody& body)
	{
		if (!m_behavior) return;
		m_behavior->SyncReplayState(body);
	}

	RigidState RigidBody::GetMainState() const
	{
		JAM_ASSERT(m_mainActor)

		if (m_behavior) return m_mainState;

		if (auto* dyn = m_mainActor->is<PxRigidDynamic>())
		{
			if (dyn->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
				return m_mainState;

			RigidState out = m_mainState;
			out.pose   = ToPx(dyn->getGlobalPose());
			out.linVel = ToPx(dyn->getLinearVelocity());
			out.angVel = ToPx(dyn->getAngularVelocity());
			return out;
		}

		if (m_mainActor->is<PxRigidStatic>())
		{
			RigidState out = m_mainState;
			out.pose   = ToPx(m_mainActor->getGlobalPose());
			out.linVel = Vec3::Zero();
			out.angVel = Vec3::Zero();
			return out;
		}

		return m_mainState;
	}

	RigidState RigidBody::GetReplayState() const
	{
		if (!m_replayActor)
			return m_replayState;

		if (m_behavior) return m_replayState;

		if (auto* dyn = m_replayActor->is<PxRigidDynamic>())
		{
			if (dyn->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
				return m_replayState;

			RigidState out = m_replayState;
			out.pose   = ToPx(dyn->getGlobalPose());
			out.linVel = ToPx(dyn->getLinearVelocity());
			out.angVel = ToPx(dyn->getAngularVelocity());
			return out;
		}

		if (m_replayActor->is<PxRigidStatic>())
		{
			RigidState out = m_replayState;
			out.pose   = ToPx(m_replayActor->getGlobalPose());
			out.linVel = Vec3::Zero();
			out.angVel = Vec3::Zero();
			return out;
		}

		return m_replayState;
	}

	void RigidBody::ApplyMainState(const RigidState& s, bool kinematicLike)
	{
		m_mainState = s;

		if (kinematicLike && m_behavior && m_behavior->ApplyMainState(s))
			return;

		ApplyStateToActor(m_mainActor, s, kinematicLike);
	}

	void RigidBody::ApplyReplayState(const RigidState& s, bool kinematicLike)
	{
		m_replayState = s;

		if (kinematicLike && m_behavior && m_behavior->ApplyReplayState(s))
			return;

		ApplyStateToActor(m_replayActor, s, kinematicLike);
	}

	void RigidBody::ApplyStateBoth(const RigidState& s, bool kinematicLike)
	{
		ApplyMainState(s, kinematicLike);
		ApplyReplayState(s, kinematicLike);
	}
} // namespace jam::px
