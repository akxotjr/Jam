#pragma once

#include "jampx/api/PhysicsTypes.h"

namespace jam::px
{
	static PxVec3 ToPhysX(const Vec3& v)
	{
		return { v.x, v.y, v.z };
	}

	static PxExtendedVec3 ToPhysXEx(const Vec3& v)
	{
		return { v.x, v.y, v.z };
	}

	static PxQuat ToPhysX(const Quat& q)
	{
		return { q.x, q.y, q.z, q.w };
	}

	static PxTransform ToPhysX(const Transform& tf)
	{
		return { ToPhysX(tf.p), ToPhysX(tf.q) };
	}

	static Vec3 ToPx(const PxVec3& v)
	{
		return { v.x, v.y, v.z };
	}

	static Vec3 ToPx(const PxExtendedVec3& p)
	{
		return { static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z) };
	}

	static Quat ToPx(const PxQuat& q)
	{
		return { q.x, q.y, q.z, q.w };
	}

	static Transform ToPx(const PxTransform& tf)
	{
		return { ToPx(tf.p), ToPx(tf.q) };
	}


	
}
