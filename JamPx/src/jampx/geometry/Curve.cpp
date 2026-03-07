#include "pch.h"
#include "jampx/geometry/Curve.h"


namespace jam::px
{
	void Curve::AddControlPoints(const PxVec3& waypoint)
	{
		m_controlPoints.push_back(waypoint);
	}

	void Curve::SetControlPoints(const std::vector<PxVec3>& waypoints)
	{
		m_controlPoints = waypoints;
	}

	void Curve::ClearControlPoints()
	{
		m_controlPoints.clear();
		m_nodes.clear();
	}
} // namespace jam::px
