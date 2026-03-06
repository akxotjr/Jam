#include "pch.h"
#include "jampx/geometry/CatmullRom.h"


namespace jam::px
{
	CatmullRom::CatmullRom(float alpha)
		: m_alpha(alpha)
	{
        m_type = eCurveType::CatmullRom;
	}

	PxVec3 CatmullRom::Evaluate(float t) const
	{
        const int32 n = static_cast<int32>(m_waypoints.size());
        if (n == 0)     return { PxZero };
        if (n == 1)     return m_waypoints[0];
        if (t <= 0.f)   return m_waypoints.front();
        if (t >= 1.f)   return m_waypoints.back();

        const int   numSegments = n - 1;
        const float scaled      = t * static_cast<float>(numSegments);
        int         seg         = static_cast<int>(scaled);

        if (seg >= numSegments) seg = numSegments - 1;
        const float localT = scaled - static_cast<float>(seg);

        auto getWaypoint = [&](int i) -> const PxVec3&
            {
                return m_waypoints[static_cast<size_t>(PxClamp(i, 0, n - 1))];
            };

        return EvaluateSegment(
            getWaypoint(seg - 1),
            getWaypoint(seg),
            getWaypoint(seg + 1),
            getWaypoint(seg + 2),
            localT);
	}

	void CatmullRom::Build(uint32 segments)
	{
        m_nodes.clear();
        if (m_waypoints.size() < 2 || segments == 0)
            return;

        m_nodes.reserve(segments + 1);
        for (uint32 i = 0; i <= segments; ++i)
            m_nodes.push_back(Evaluate(static_cast<float>(i) / static_cast<float>(segments)));
	}

	PxVec3 CatmullRom::EvaluateSegment(const PxVec3& p0, const PxVec3& p1, const PxVec3& p2, const PxVec3& p3, float localT) const
	{
        auto knotInterval = [this](const PxVec3& a, const PxVec3& b) -> float
            {
                if (m_alpha < 1e-6f) return 1.f;
                const float d = (b - a).magnitude();
                return (d < 1e-7f) ? 1e-7f : powf(d, m_alpha);
            };

        const float t0 = 0.f;
        const float t1 = t0 + knotInterval(p0, p1);
        const float t2 = t1 + knotInterval(p1, p2);
        const float t3 = t2 + knotInterval(p2, p3);

        const float t = t1 + localT * (t2 - t1);

        auto lerpKnot = [](const PxVec3& a, const PxVec3& b, float ta, float tb, float tv) -> PxVec3
            {
                if (fabsf(tb - ta) < 1e-7f) return (a + b) * 0.5f;
                return a + (b - a) * ((tv - ta) / (tb - ta));
            };

        const PxVec3 A1 = lerpKnot(p0, p1, t0, t1, t);
        const PxVec3 A2 = lerpKnot(p1, p2, t1, t2, t);
        const PxVec3 A3 = lerpKnot(p2, p3, t2, t3, t);

        const PxVec3 B1 = lerpKnot(A1, A2, t0, t2, t);
        const PxVec3 B2 = lerpKnot(A2, A3, t1, t3, t);

        return lerpKnot(B1, B2, t1, t2, t);
	}
}
