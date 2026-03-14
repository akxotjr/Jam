#include "pch.h"
#include "jampx/geometry/BSpline.h"


namespace jam::px
{
	BSpline::BSpline(uint32 degree)
		: m_degree(degree)
	{
		m_type = eCurveType::BSpline;
	}

	void BSpline::SetDegree(uint32 degree)
	{
		m_degree = degree;
		m_knots.clear();
	}

	PxVec3 BSpline::Evaluate(float t) const
	{
		const int32 n = static_cast<int32>(m_controlPoints.size());
		const int32 p = static_cast<int32>(m_degree);

		if (n <= p)   return { physx::PxZero };
		if (t <= 0.f) return m_controlPoints.front();
		if (t >= 1.f) return m_controlPoints.back();

		if (static_cast<int32>(m_knots.size()) != n + p + 1)
			BuildKnots();

		PxVec3 result(0.f, 0.f, 0.f);
		for (int32 i = 0; i < n; ++i)
			result += m_controlPoints[i] * BasisFunc(i, p, t);

		return result;
	}

	void BSpline::Build(uint32 segments)
	{
		m_nodes.clear();
		const int32 n = static_cast<int32>(m_controlPoints.size());
		const int32 p = static_cast<int32>(m_degree);

		if (n <= p || segments == 0)
			return;

		BuildKnots();

		m_nodes.reserve(segments + 1);
		for (uint32 i = 0; i <= segments; ++i)
			m_nodes.push_back(Evaluate(static_cast<float>(i) / static_cast<float>(segments)));
	}

	void BSpline::BuildKnots() const
	{
		const int32 n = static_cast<int32>(m_controlPoints.size()) - 1;
		const int32 p = static_cast<int32>(m_degree);
		const int32 m = n + p + 1;
		m_knots.resize(m + 1);

		for (int32 i = 0; i <= p; ++i)
			m_knots[i] = 0.f;

		const int32 interior = m - 2 * p;
		for (int32 i = 1; i < interior; ++i)
			m_knots[p + i] = static_cast<float>(i) / static_cast<float>(interior);

		for (int32 i = m - p; i <= m; ++i)
			m_knots[i] = 1.f;
	}

	float BSpline::BasisFunc(int32 i, int32 p, float t) const
	{
		if (p == 0)
			return (m_knots[i] <= t && t < m_knots[i + 1]) ? 1.f : 0.f;

		float left = 0.f, right = 0.f;

		const float d1 = m_knots[i + p] - m_knots[i];
		if (d1 > 1e-7f)
			left = (t - m_knots[i]) / d1 * BasisFunc(i, p - 1, t);

		const float d2 = m_knots[i + p + 1] - m_knots[i + 1];
		if (d2 > 1e-7f)
			right = (m_knots[i + p + 1] - t) / d2 * BasisFunc(i + 1, p - 1, t);

		return left + right;
	}
}
