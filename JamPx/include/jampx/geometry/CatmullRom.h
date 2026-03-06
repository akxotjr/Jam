#pragma once
#include "jampx/geometry/Curve.h"


namespace jam::px
{
	class CatmullRom : public Curve
	{
    public:
        explicit CatmullRom(float alpha = 0.5f);

        void    SetAlpha(float alpha) { m_alpha = PxClamp(alpha, 0.f, 1.f); }
        float   GetAlpha() const noexcept { return m_alpha; }

        PxVec3  Evaluate(float t) const override;
        void    Build(uint32 segments) override;

    private:
        PxVec3  EvaluateSegment(const PxVec3& p0, const PxVec3& p1, const PxVec3& p2, const PxVec3& p3, float localT) const;

	private:
        float   m_alpha = 0.5f;
	};
}

