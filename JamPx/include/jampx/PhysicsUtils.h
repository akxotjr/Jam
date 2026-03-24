#pragma once

#include "Easing.h"
#include "jampx/PhysicsTypes.h"

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

	// a: current, b: previous
	static PxVec3 GetLinearVelocity(const PxVec3& a, const PxVec3& b, const float dt)
	{
		if (dt <= 0.f) return PxVec3(physx::PxZero);
		return (a - b) * (1.0f / dt);
	}

	// a: current, b: previous
	static PxVec3 GetAngularVelocity(const PxQuat& a, const PxQuat& b, const float dt)
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


	static bool IsNearlyZero(const float v, const float eps = EPSILON)
	{
		return std::fabs(v) <= eps;
	}

	static bool IsNearlyZero(const PxVec3& v, const float eps = EPSILON)
	{
		return v.magnitudeSquared() <= eps * eps;
	}

	static bool IsNearlyEqual(const float a, const float b, const float eps = EPSILON)
	{
		return std::fabs(a - b) <= eps;
	}

	static bool IsNearlyEqual(const PxVec3& a, const PxVec3& b, float eps = EPS_3)
	{
		return (a - b).magnitudeSquared() <= eps * eps;
	}

	static bool IsNearlyEqual(const PxQuat& a, const PxQuat& b, float eps = EPS_4)
	{
		return std::fabs(a.dot(b)) >= (1.0f - eps);
	}

	static bool IsNearlyEqual(const PxTransform& a, const PxTransform& b, const float posEps = EPS_3, const float rotEps = EPS_4)
	{
		if (!IsNearlyEqual(a.p, b.p, posEps)) return false;
		return IsNearlyEqual(a.q, b.q, rotEps);
	}


	static PxVec3 SafeNormalize(const PxVec3& v, const PxVec3& fallback = PxVec3(1.f, 0.f, 0.f))
	{
		if (IsNearlyZero(v)) return fallback;
		return v.getNormalized();
	}

	static PxVec3 RotateTowards(const PxVec3& fromDir, const PxVec3& toDir, float maxRad)
	{
		const PxVec3 fromN = SafeNormalize(fromDir);
		const PxVec3 toN = SafeNormalize(toDir, fromN);

		const float d = physx::PxClamp(fromN.dot(toN), -1.f, 1.f);
		const float angle = acosf(d);

		if (angle <= maxRad || angle <= 1e-6f)
			return toN;

		PxVec3 axis = fromN.cross(toN);
		if (IsNearlyZero(axis))
		{
			axis = fromN.cross(PxVec3(0.f, 1.f, 0.f));
			if (IsNearlyZero(axis))
				axis = fromN.cross(PxVec3(1.f, 0.f, 0.f));
		}
		axis = SafeNormalize(axis);

		const PxQuat q(maxRad, axis);
		return SafeNormalize(q.rotate(fromN), fromN);
	}

	static PxVec3 ClampMagnitude(const PxVec3& v, float maxMag)
	{
		const float m = v.magnitude();
		if (m <= maxMag || m <= EPSILON)
			return v;
		return v * (maxMag / m);
	}
	
} // namespace jam::px
