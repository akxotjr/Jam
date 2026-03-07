#include "pch.h"
#include "jampx/geometry/Bezier.h"


namespace jam::px
{
	Bezier::Bezier()
	{
		m_type = eCurveType::Bezier;
	}

	PxVec3 Bezier::Evaluate(float t) const
	{
		if (m_controlPoints.empty())	 return {physx::PxZero };
		if (m_controlPoints.size() == 1) return m_controlPoints[0];

		std::vector<PxVec3> pts = m_controlPoints;
		const int32 n = static_cast<int32>(pts.size());
		for (int32 r = 1; r < n; ++r)
			for (int32 i = 0; i < n - r; ++i)
				pts[i] = pts[i] * (1.f - t) + pts[i + 1] * t;

		return pts[0];
	}

	void Bezier::Build(uint32 segments)
	{
		m_nodes.clear();
		if (m_controlPoints.size() < 2 || segments == 0)
			return;

		m_nodes.reserve(segments + 1);
		for (uint32 i = 0; i <= segments; ++i)
			m_nodes.push_back(Evaluate(static_cast<float>(i) / static_cast<float>(segments)));
	}
}
