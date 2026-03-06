#pragma once

#include "character/CharacterMovementTypes.h"
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

	static ObjectId GetObjectId(const PxActor* actor) noexcept
	{
		if (!actor) return INVALID_OBJ_ID;
		return static_cast<ObjectId>(reinterpret_cast<uintptr_t>(actor->userData));
	}


	static CharacterUserData* GetCharacterUserData(const PxController& cct)
	{
		return static_cast<CharacterUserData*>(cct.getUserData());
	}
	

	static PxVec3 GetLinearVelocity(const PxVec3& a, const PxVec3& b, float dt)
	{

	}

	static PxVec3 GetAngularVelocity(const PxQuat& a, const PxQuat& b, float dt)
	{
		if (dt <= 0.f) return PxVec3(PxZero);

		PxQuat dq = a * b.getConjugate();
		dq.normalize();

		float angle = 0.f;
		PxVec3 axis(0, 0, 1);
		dq.toRadiansAndUnitAxis(angle, axis);

		// map angle to [-pi, pi] for stability
		if (angle > PxPi) angle -= PxTwoPi;

		return axis * (angle / dt);
	}


	inline PxVec3 CatmullRomCentripetal(const PxVec3& P0, const PxVec3& P1, const PxVec3& P2, const PxVec3& P3, float u01, const float alpha)
	{
		// Build knot parameters
		const float a = alpha;
		float t0 = 0.f;
		float t1 = t0 + CR_TimeAdvance(P0, P1, a);
		float t2 = t1 + CR_TimeAdvance(P1, P2, a);
		float t3 = t2 + CR_TimeAdvance(P2, P3, a);

		// Handle degenerates (duplicate points): add eps to keep denominators non-zero
		const float eps = 1e-4f;
		if (t1 - t0 < eps) t1 = t0 + eps;
		if (t2 - t1 < eps) t2 = t1 + eps;
		if (t3 - t2 < eps) t3 = t2 + eps;

		// Map u in [0,1] to t in [t1,t2]
		float t = Lerp(t1, t2, Clamp01(u01));

		// Interpolate with the centripetal formula (as per "parameterized Catmull-Rom")
		PxVec3 A1 = ((t1 - t) / (t1 - t0)) * P0 + ((t - t0) / (t1 - t0)) * P1;
		PxVec3 A2 = ((t2 - t) / (t2 - t1)) * P1 + ((t - t1) / (t2 - t1)) * P2;
		PxVec3 A3 = ((t3 - t) / (t3 - t2)) * P2 + ((t - t2) / (t3 - t2)) * P3;

		PxVec3 B1 = ((t2 - t) / (t2 - t0)) * A1 + ((t - t0) / (t2 - t0)) * A2;
		PxVec3 B2 = ((t3 - t) / (t3 - t1)) * A2 + ((t - t1) / (t3 - t1)) * A3;

		PxVec3 C = ((t2 - t) / (t2 - t1)) * B1 + ((t - t1) / (t2 - t1)) * B2;


		return C;
	}


}
