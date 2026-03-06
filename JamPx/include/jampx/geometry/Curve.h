#pragma once


namespace jam::px
{
	enum class eCurveType : uint8
	{
		None,
		CatmullRom,
		BSpline,
		Bezier,
	};

	class Curve
	{
	public:
		Curve() = default;
		virtual ~Curve() = default;

		void					AddWaypoint(const PxVec3& waypoint);
		void					SetWaypoint(const vector<PxVec3>& waypoints);
		void					ClearWaypoints();

		eCurveType				GetCurveType() const noexcept { return m_type; }
		const vector<PxVec3>&	GetWaypoints() const noexcept { return m_waypoints; }
		const vector<PxVec3>&	GetNodes() const noexcept { return m_nodes; }
		bool					IsEmpty() const noexcept { return m_waypoints.empty(); }

		virtual PxVec3			Evaluate(float t) const = 0;

		virtual void			Build(uint32 segments) = 0;

	protected:
		vector<PxVec3>			m_waypoints;		// 
		vector<PxVec3>			m_nodes;
		eCurveType				m_type = eCurveType::None;
	};
}
