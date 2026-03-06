#include "pch.h"
#include "jampx/kinematic/DefaultKinematicDriver.h"

#include <algorithm>


namespace jam::px
{
	DefaultKinematicDriver::DefaultKinematicDriver(const DefaultKinematicDriverConfig& cfg)
		: m_config(cfg)
	{
		if (!cfg.waypoints.empty())
		{
			m_state.pose		= cfg.waypoints[0].pose;
			m_state.pauseTimer	= cfg.waypoints[0].pauseDuration;
		}
	}


	Transform DefaultKinematicDriver::Tick(float dt)
	{
		const int32 n = static_cast<int32>(m_config.waypoints.size());
		if (n == 0 || m_state.done) return m_state.pose;

		if (m_state.pauseTimer > 0.f)
		{
			m_state.pauseTimer -= dt;
			return m_state.pose;
		}

		if (n == 1)
		{
			m_state.pose = m_config.waypoints[0].pose;
			return m_state.pose;
		}

		const int32      nextIdx = NextIndex();
		const Transform& from	 = m_config.waypoints[m_state.segmentIdx].pose;
		const Transform& to		 = m_config.waypoints[nextIdx].pose;

		const float segLen = from.p.Distance(to.p);
		if (segLen < EPSILON)
		{
			AdvanceSegment();
			return m_state.pose;
		}

		m_state.segProgress += (m_config.speed * dt) / segLen;

		if (m_state.segProgress >= 1.f)
		{
			m_state.pose = to;
			AdvanceSegment();
		}
		else
		{
			m_state.pose.p = Vec3::Lerp(from.p, to.p, m_state.segProgress);
			m_state.pose.q = Quat::Lerp(from.q, to.q, m_state.segProgress);
		}

		return m_state.pose;
	}

	int32 DefaultKinematicDriver::NextIndex() const
	{
		const int32 n = static_cast<int32>(m_config.waypoints.size());
		const int32 next = m_state.segmentIdx + m_state.direction;

		if (m_config.pathMode == eKinematicPathMode::Loop)
			return (next + n) % n;

		return std::clamp(next, 0, n - 1);
	}

	void DefaultKinematicDriver::AdvanceSegment()
	{
		const int32 n = static_cast<int32>(m_config.waypoints.size());
		if (n <= 1)
		{
			m_state.done = true;
			return;
		}

		const int32 next = NextIndex();

		if (m_config.pathMode == eKinematicPathMode::Once && next == m_state.segmentIdx)
		{
			m_state.done = true;
			return;
		}

		if (m_config.pathMode == eKinematicPathMode::PingPong)
		{
			const int32 afterNext = next + m_state.direction;
			if (afterNext < 0 || afterNext >= n)
				m_state.direction = -m_state.direction;
		}

		m_state.segmentIdx = next;
		m_state.segProgress = 0.f;
		m_state.pauseTimer = m_config.waypoints[next].pauseDuration;
	}
}
