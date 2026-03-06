#pragma once
#include "jampx/geometry/Curve.h"


namespace jam::px
{
	/**
	 * @brief Bezier Curve. (using De Casteljau algorithm, control points count = degree + 1)
	 * 
	 * @note De Casteljau Algorithm:
	 * multilinear construction (six times of linear interpolations)
	 * 
	 * control points = b0...b3
	 *  b01(t)  = lerp(b0, b1, t)
	 *  b12(t)  = lerp(b1, b2, t)
	 *  b23(t)  = lerp(b2, b3, t)
	 *  b012(t) = lerp(b01, b12, t)
	 *  b123(t) = lerp(b12, b23, t)
	 *  
	 *  P(t)	= lerp(b012, b123, t)
	 */
	class Bezier : public Curve
	{
	public:
		Bezier();

		PxVec3	Evaluate(float t) const override;
		void	Build(uint32 segments) override;
	};

}
