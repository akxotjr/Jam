#pragma once
#include "jampx/geometry/Curve.h"

namespace jam::px
{
	class BSpline : public Curve
	{
	public:
		explicit BSpline(uint32 degree = 3);

		void		SetDegree(uint32 degree);
		uint32		GetDegree() const noexcept { return m_degree; }

		PxVec3		Evaluate(float t) const override;
		void		Build(uint32 segments) override;

	private:
		void		BuildKnots() const;
		float		BasisFunc(int32 i, int32 p, float t) const;

	private:
		uint32					m_degree = 3;
		mutable vector<float>	m_knots;
	};
}

