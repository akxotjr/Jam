#pragma once

#include "Easing.h"

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

	static ObjectId GetObjectId(const PxActor* actor) noexcept
	{
		if (!actor) return INVALID_OBJ_ID;
		return static_cast<ObjectId>(reinterpret_cast<uintptr_t>(actor->userData));
	}


	static PxVec3 GetLinearVelocity(const PxVec3& a, const PxVec3& b, float dt)
	{

	}

	static PxVec3 GetAngularVelocity(const PxQuat& a, const PxQuat& b, float dt)
	{
		if (dt <= 0.f) return PxVec3(physx::PxZero);

		PxQuat dq = a * b.getConjugate();
		dq.normalize();

		float angle = 0.f;
		PxVec3 axis(0, 0, 1);
		dq.toRadiansAndUnitAxis(angle, axis);

		// map angle to [-pi, pi] for stability
		if (angle > physx::PxPi) angle -= physx::PxTwoPi;

		return axis * (angle / dt);
	}

	
}
