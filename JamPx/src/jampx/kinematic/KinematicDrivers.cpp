#include "pch.h"
#include "jampx/kinematic/KinematicDrivers.h"

#include <algorithm>

#include <physx/extensions/PxSimpleFactory.h>
#include <physx/extensions/PxSamplingExt.h>

#include "jampx/geometry/Bezier.h"
#include "jampx/geometry/BSpline.h"
#include "jampx/geometry/CatmullRom.h"

namespace jam::px
{

	// -----------------------------------------------------------
	// Waypoint
	// -----------------------------------------------------------

	WaypointKinematicDriver::WaypointKinematicDriver(KinematicCommon common, WaypointSource src)
		: m_common(common), m_src(src)
	{
		if (!src.waypoints.empty())
		{
			m_pose		 = src.waypoints[0].pose;
			m_pauseTimer = src.waypoints[0].pauseDuration;
		}
	}

	PxTransform WaypointKinematicDriver::Tick(float dt)
	{
		const int32 n = static_cast<int32>(m_src.waypoints.size());
		if (n == 0 || m_done) return m_pose;

		if (m_pauseTimer > 0.f)
		{
			m_pauseTimer -= dt;
			return m_pose;
		}

		if (n == 1) return m_pose;
	
		const int32			nextIdx = NextIndex();
		const PxTransform&	from	 = m_src.waypoints[m_segIndex].pose;
		const PxTransform&	to		 = m_src.waypoints[nextIdx].pose;

		const float segDist = (from.p - to.p).magnitude();
		if (segDist < EPSILON)
		{
			AdvanceSegment();
			return m_pose;
		}

		m_segProgress += (m_src.speed * dt) / segDist;

		if (m_segProgress >= 1.f)
		{
			m_pose = to;
			AdvanceSegment();
		}
		else
		{
			m_pose.p = Vec3::Lerp(from.p, to.p, m_segProgress);
			m_pose.q = Quat::Lerp(from.q, to.q, m_segProgress);

			m_pose.p = 
		}

		return m_pose;
	}

	int32 WaypointKinematicDriver::NextIndex() const
	{
		const int32 n	 = static_cast<int32>(m_src.waypoints.size());
		const int32 next = m_segIndex + m_dir;

		if (m_src.loopMode == eWaypointLoop::Loop)
			return (next + n) % n;

		return std::clamp(next, 0, n - 1);
	}

	void WaypointKinematicDriver::AdvanceSegment()
	{
		const int32 n = static_cast<int32>(m_src.waypoints.size());
		if (n <= 1)
		{
			m_done = true;
			return;
		}

		const int32 next = NextIndex();

		if (m_src.loopMode == eWaypointLoop::Once && next == m_segIndex)
		{
			m_done = true;
			return;
		}

		if (m_src.loopMode == eWaypointLoop::PingPong)
		{
			const int32 afterNext = next + m_dir;
			if (afterNext < 0 || afterNext >= n)
				m_dir = -m_dir;
		}

		m_segIndex		= next;
		m_segProgress	= 0.f;
		m_pauseTimer	= m_src.waypoints[next].pauseDuration;
	}



	// -----------------------------------------------------------
	// Spline
	// -----------------------------------------------------------

	namespace 
	{
		unique_ptr<Curve> MakeCurve(const SplineSource& src)
		{
			switch (src.type)
			{
			case eCurveType::CatmullRom:
				{
					auto c = std::make_unique<CatmullRom>(src.alpha);
					c->SetWaypoint(src.controlPoints);
					c->Build(src.buildSegments);
					return c;
				}
			case eCurveType::BSpline:
				{
					auto c = std::make_unique<BSpline>(src.degree);
					c->SetWaypoint(src.controlPoints);
					c->Build(src.buildSegments);
					return c;
				}
			case eCurveType::Bezier:
				{
					auto c = std::make_unique<Bezier>();
					c->SetWaypoint(src.controlPoints);
					c->Build(src.buildSegments);
					return c;
				}
			}

			return nullptr;
		}
	}


	SplineKinematicDriver::SplineKinematicDriver(KinematicCommon common, SplineSource src)
		: m_common(common), m_src(std::move(src)), m_curve(MakeCurve(m_src))
	{
		BuildArchLengthLUT();
	}

	PxTransform SplineKinematicDriver::Tick(float dt)
	{
		if (!m_curve || m_totalLength < 1e-7f)
			return {};

		m_arcPos += m_src.speed * dt;

		if (m_src.loop)
		{
			// 루프: 전체 길이로 wrap
			while (m_arcPos >= m_totalLength)
				m_arcPos -= m_totalLength;
		}
		else
		{
			m_arcPos = PxMin(m_arcPos, m_totalLength);
		}

		const float t = ArcLengthToT(m_arcPos);

		// 현재 위치
		const PxVec3 pos = m_curve->Evaluate(t);

		// 진행 방향 → 회전 (tangent 근사)
		constexpr float kEps	= 1e-3f;
		const float     tFwd	= PxMin(t + kEps, 1.f);
		const PxVec3    fwd		= (m_curve->Evaluate(tFwd) - pos);
		const float     fwdLen	= fwd.magnitude();

		PxTransform result{};
		result.p = { pos.x, pos.y, pos.z };

		if (fwdLen > 1e-6f)
		{
			const PxVec3 dir	= fwd / fwdLen;
			// Y-up 기준 look-rotation
			const PxVec3 up		= { 0, 1, 0 };
			const PxVec3 right	= up.cross(dir).getNormalized();
			const PxVec3 realUp = dir.cross(right).getNormalized();

			// 3x3 → quaternion
			const float trace = right.x + realUp.y + dir.z;
			if (trace > 0.f)
			{
				const float s = 0.5f / sqrtf(trace + 1.f);
				result.q = { (realUp.z - dir.y) * s, (dir.x - right.z) * s, (right.y - realUp.x) * s, 0.25f / s };
			}
		}

		return result;
	}

	void SplineKinematicDriver::BuildArchLengthLUT()
	{
		const vector<PxVec3>& nodes = m_curve->GetNodes();
		m_lut.resize(nodes.size(), 0.f);

		for (size_t i = 1; i < nodes.size(); ++i)
			m_lut[i] = m_lut[i - 1] + (nodes[i] - nodes[i - 1]).magnitude();

		m_totalLength = m_lut.empty() ? 0.f : m_lut.back();
	}

	float SplineKinematicDriver::ArcLengthToT(float arcLen) const
	{
		if (m_lut.size() < 2 || m_totalLength < 1e-7f)
			return 0.f;

		// 이진 탐색으로 구간 찾기
		const auto it = ranges::lower_bound(m_lut, arcLen);
		if (it == m_lut.end())
			return 1.f;

		const size_t i = std::distance(m_lut.begin(), it);
		if (i == 0)
			return 0.f;

		const float segLen = m_lut[i] - m_lut[i - 1];
		const float localT = (segLen < 1e-7f) ? 0.f : (arcLen - m_lut[i - 1]) / segLen;

		const float tPerNode = 1.f / static_cast<float>(m_lut.size() - 1);
		return ((i - 1) + localT) * tPerNode;
	}





	// -----------------------------------------------------------
	// Spline
	// -----------------------------------------------------------

	namespace 
	{
		PxVec3 AxisFromPlaneMode(eOrbitPlaneMode mode, const PxVec3& customNormal) noexcept
		{
			switch (mode)
			{
			case eOrbitPlaneMode::XY:     return { 0, 0, 1 };
			case eOrbitPlaneMode::XZ:     return { 0, 1, 0 };
			case eOrbitPlaneMode::YZ:     return { 1, 0, 0 };
			case eOrbitPlaneMode::Custom: return customNormal.getNormalized();
			}
			return { 0, 1, 0 };
		}

		// 3x3 rotation (column: right | up | fwd) → Quat — Full Shepperd
		PxQuat MatToQuat(const PxVec3& right, const PxVec3& up, const PxVec3& fwd)
		{
			const float trace = right.x + up.y + fwd.z;
			if (trace > 0.f)
			{
				const float s = 0.5f / sqrtf(trace + 1.f);
				return { (up.z - fwd.y) * s, (fwd.x - right.z) * s, (right.y - up.x) * s, 0.25f / s };
			}
			else if (right.x > up.y && right.x > fwd.z)
			{
				const float s = 2.f * sqrtf(1.f + right.x - up.y - fwd.z);
				return { 0.25f * s, (up.x + right.y) / s, (right.z + fwd.x) / s, (up.z - fwd.y) / s };
			}
			else if (up.y > fwd.z)
			{
				const float s = 2.f * sqrtf(1.f + up.y - right.x - fwd.z);
				return { (up.x + right.y) / s, 0.25f * s, (fwd.y + up.z) / s, (fwd.x - right.z) / s };
			}
			else
			{
				const float s = 2.f * sqrtf(1.f + fwd.z - right.x - up.y);
				return { (right.z + fwd.x) / s, (fwd.y + up.z) / s, 0.25f * s, (right.y - up.x) / s };
			}
		}

		PxQuat BuildLookRotation(const PxVec3& fwd, const PxVec3& up)
		{
			const PxVec3 right = up.cross(fwd).getNormalized();
			const PxVec3 realUp = fwd.cross(right).getNormalized();
			return MatToQuat(right, realUp, fwd);
		}

	}

	OrbitKinematicDriver::OrbitKinematicDriver(KinematicCommon common, OrbitSource src)
		: m_common(common), m_src(src), m_angle(src.initialAngleRad)
	{
		// planeMode → axis → Gram-Schmidt로 궤도 평면 기저 사전 계산
		m_axisN = AxisFromPlaneMode(m_src.planeMode, m_src.customPlaneNormal);

		const PxVec3 arbitrary = (fabsf(m_axisN.dot({ 0, 1, 0 })) < 0.99f)
			? PxVec3{ 0, 1, 0 }
		: PxVec3{ 1, 0, 0 };

		m_basisR = (arbitrary - m_axisN * m_axisN.dot(arbitrary)).getNormalized();
		m_basisF = m_axisN.cross(m_basisR).getNormalized();

		// FollowTarget 모드에서 SetDynamicCenter 호출 전 fallback
		m_dynamicCenter = m_src.fixedCenter;
	}

	PxTransform OrbitKinematicDriver::Tick(float dt)
	{
		if (m_done)
			return ComputePosition(m_angle);    // done 상태에서도 마지막 pose 유지

		AdvanceAngle(dt);

		const PxVec3 pos = ComputePosition(m_angle);

		PxTransform result{};
		result.p = { pos.x, pos.y, pos.z };
		result.q = ComputeRotation(m_angle, pos);

		return result;
	}



	PxVec3 OrbitKinematicDriver::ComputeCenter() const
	{
		switch (m_src.centerMode)
		{
		case eOrbitCenterMode::FixedPoint:
			return m_src.fixedCenter;
		case eOrbitCenterMode::FollowTarget:
			return m_dynamicCenter + m_src.targetOffset;
		}
		return m_src.fixedCenter;
	}

	PxVec3 OrbitKinematicDriver::ComputePosition(float angle) const
	{
		const PxVec3 center = ComputeCenter();
		const float  cosA = cosf(angle);
		const float  sinA = sinf(angle);

		switch (m_src.radiusMode)
		{
		case eOrbitRadiusMode::Circle:
			return center + (m_basisR * cosA + m_basisF * sinA) * m_src.radius;

		case eOrbitRadiusMode::Ellipse:
			return center
				+ m_basisR * (m_src.ellipseRadius.x * cosA)
				+ m_basisF * (m_src.ellipseRadius.y * sinA);
		}
		return center;
	}

	PxVec3 OrbitKinematicDriver::ComputeTangent(float angle) const
	{
		const float cosA = cosf(angle);
		const float sinA = sinf(angle);

		// dPos/dAngle — radius는 정규화 시 상쇄되므로 무시
		PxVec3 dPos;
		switch (m_src.radiusMode)
		{
		case eOrbitRadiusMode::Circle:
			dPos = m_basisF * cosA - m_basisR * sinA;
			break;
		case eOrbitRadiusMode::Ellipse:
			dPos = m_basisF * (m_src.ellipseRadius.y * cosA)
				- m_basisR * (m_src.ellipseRadius.x * sinA);
			break;
		}

		const float mag = dPos.magnitude();
		if (mag < 1e-7f) return m_basisF;          // degenerate fallback

		return dPos * (m_dir / mag);                // PingPong 역방향 반영
	}

	PxQuat OrbitKinematicDriver::ComputeRotation(float angle, const PxVec3& pos) const
	{
		switch (m_src.orientationMode)
		{
		case eOrbitOrientationMode::KeepRotation:
		{
			const PxQuat& pq = m_src.initialRotation;
			return { pq.x, pq.y, pq.z, pq.w };
		}
		case eOrbitOrientationMode::FaceCenter:
		{
			// center → pos 방향: center와 pos 모두 궤도 평면 위이므로 axisN과 항상 수직
			const PxVec3 toCenter = ComputeCenter() - pos;
			if (toCenter.magnitude() < 1e-7f) break;
			return BuildLookRotation(toCenter.getNormalized(), m_axisN);
		}
		case eOrbitOrientationMode::OrientAlongVelocity:
			return BuildLookRotation(ComputeTangent(angle), m_axisN);
		}

		const PxQuat& pq = m_src.initialRotation;
		return { pq.x, pq.y, pq.z, pq.w };
	}

	void OrbitKinematicDriver::AdvanceAngle(float dt)
	{
		const float delta = m_src.angularSpeedRad * m_dir * dt;

		switch (m_src.endMode)
		{
		case eOrbitEndMode::Loop:
		{
			m_angle += delta;
			const float range = m_src.maxAngleRad - m_src.minAngleRad;
			if (range > 1e-7f)
			{
				while (m_angle > m_src.maxAngleRad)  m_angle -= range;
				while (m_angle < m_src.minAngleRad)  m_angle += range;
			}
			break;
		}
		case eOrbitEndMode::PingPong:
		{
			m_angle += delta;
			if (m_angle >= m_src.maxAngleRad)
			{
				m_angle = m_src.maxAngleRad;
				m_dir = -1.f;
			}
			else if (m_angle <= m_src.minAngleRad)
			{
				m_angle = m_src.minAngleRad;
				m_dir = 1.f;
			}
			break;
		}
		case eOrbitEndMode::Clamp:
		{
			m_angle = PxClamp(m_angle + delta, m_src.minAngleRad, m_src.maxAngleRad);
			if (m_angle >= m_src.maxAngleRad || m_angle <= m_src.minAngleRad)
				m_done = true;
			break;
		}
		}
	}
} // namespace jam::px
