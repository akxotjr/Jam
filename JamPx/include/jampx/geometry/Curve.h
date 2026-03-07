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

		void						AddControlPoints(const PxVec3& waypoint);
		void						SetControlPoints(const std::vector<PxVec3>& waypoints);
		void						ClearControlPoints();

		eCurveType					GetCurveType() const noexcept { return m_type; }
		const std::vector<PxVec3>&	GetControlPoints() const noexcept { return m_controlPoints; }
		const std::vector<PxVec3>&	GetNodes() const noexcept { return m_nodes; }
		bool						IsEmpty() const noexcept { return m_controlPoints.empty(); }

		virtual PxVec3				Evaluate(float t) const = 0;

		virtual void				Build(uint32 segments) = 0;

	protected:
		std::vector<PxVec3>			m_controlPoints;		// 
		std::vector<PxVec3>			m_nodes;
		eCurveType					m_type = eCurveType::None;
	};

} // namespace jam::px
