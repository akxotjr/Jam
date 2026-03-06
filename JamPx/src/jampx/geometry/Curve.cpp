#include "pch.h"
#include "jampx/geometry/Curve.h"


namespace jam::px
{
	void Curve::AddWaypoint(const PxVec3& waypoint)
	{
		m_waypoints.push_back(waypoint);
	}

	void Curve::SetWaypoint(const vector<PxVec3>& waypoints)
	{
		m_waypoints = waypoints;
	}

	void Curve::ClearWaypoints()
	{
		m_waypoints.clear();
		m_nodes.clear();
	}
} // namespace jam::px
