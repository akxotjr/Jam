#include "pch.h"
#include "jampx/character/ExternalMoveAccumulator.h"


namespace jam::px
{
	void ExternalMoveAccumulator::Clear()
	{
        m_addVel         = Vec3::Zero();
        m_addDisp        = Vec3::Zero();
        m_override       = false;
        m_overrideWeight = 0.0f;
	}

	void ExternalMoveAccumulator::Add(const ExternalMoveRequest& r)
	{
        m_addVel  += r.addVelocity     * r.overrideWeight;
        m_addDisp += r.addDisplacement * r.overrideWeight;

        if (r.overrideLocomotion)
        {
            m_override       = true;
            m_overrideWeight = std::max(m_overrideWeight, r.overrideWeight);
        }
	}
}
