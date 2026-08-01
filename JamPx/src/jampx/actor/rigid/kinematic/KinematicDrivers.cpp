#include "pch.h"
#include "jampx/actor/rigid/kinematic/KinematicDrivers.h"

#include <algorithm>
#include <iostream>

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
			const float easedProgress = m_src.useEaseProfile
				? Ease::ApplyProfile(m_segProgress, 1.f, m_src.easeProfile)
				: Ease::Evaluate(m_src.easeType, m_segProgress);

			m_pose.p = Ease::LerpEase<PxVec3>(from.p, to.p, easedProgress, m_src.easeType);
			m_pose.q = Ease::Slerp(from.q, to.q, easedProgress);
		}

		return m_pose;
	}

	KinematicState WaypointKinematicDriver::BuildState() const
	{
		KinematicState state{};
		state.phase	  = static_cast<uint32>(std::max(0, m_segIndex));
		state.t		  = std::clamp(m_segProgress, 0.0f, 1.0f);
		return state;
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
		std::unique_ptr<Curve> MakeCurve(const CurveSource& src)
		{
			switch (src.type)
			{
			case eCurveType::CatmullRom:
				{
					auto c = std::make_unique<CatmullRom>(src.alpha);
					c->SetControlPoints(src.controlPoints);
					c->Build(src.buildSegments);
					return c;
				}
			case eCurveType::BSpline:
				{
					auto c = std::make_unique<BSpline>(src.degree);
					c->SetControlPoints(src.controlPoints);
					c->Build(src.buildSegments);
					return c;
				}
			case eCurveType::Bezier:
				{
					auto c = std::make_unique<Bezier>();
					c->SetControlPoints(src.controlPoints);
					c->Build(src.buildSegments);
					return c;
				}
			}

			return nullptr;
		}
	}


	CurveKinematicDriver::CurveKinematicDriver(KinematicCommon common, CurveSource src)
		: m_common(common), m_src(std::move(src)), m_curve(MakeCurve(m_src))
	{
		BuildArchLengthLUT();

		if (m_src.duration <= 1e-6f && m_src.speed > 1e-6f && m_totalLength > 1e-6f)
			m_src.duration = m_totalLength / m_src.speed;

		if (m_curve)
			m_pose.p = m_curve->Evaluate(0.f);
	}

	PxTransform CurveKinematicDriver::Tick(float dt)
	{
		if (!m_curve || m_totalLength < 1e-7f)
			return {};

		m_elapsedTime += dt;

		float progress = (m_src.duration > 1e-6f) ? (m_elapsedTime / m_src.duration) : 1.f;

		if (m_src.loop)
		{
			progress = progress - std::floor(progress);
		}
		else
		{
			progress = physx::PxClamp(progress, 0.f, 1.0f);

			// done 플래그는 이번 프레임 pose를 끝점으로 계산한 뒤 다음 Tick에서 반환되도록
			// 이번 Tick 안에서 계산은 계속 진행하고, 마지막에 m_done 처리
		}
		const float distRatio = m_src.useEaseProfile
			? Ease::ApplyProfile(progress, m_src.duration, m_src.easeProfile)
			: Ease::Evaluate(m_src.easeType, progress);

		m_arcPos = distRatio * m_totalLength;

		const float  t	 = ArcLengthToT(m_arcPos);
		const PxVec3 pos = m_curve->Evaluate(t);

		constexpr float kEps = 1e-3f;
		float tFwd = t + kEps;

		if (m_src.loop)
		{
			if (tFwd > 1.f) tFwd -= 1.f;
		}
		else
		{
			tFwd = physx::PxMin(tFwd, 1.f);
		}

		const PxVec3 posFwd = m_curve->Evaluate(tFwd);
		const PxVec3 fwd    = posFwd - pos;
		const float  fwdLen = fwd.magnitude();

		m_pose.p = pos;
		m_pose.q = PxQuat(physx::PxIdentity);

		if (fwdLen > 1e-6f)
		{
			const PxVec3 dir	 = fwd / fwdLen;
			const PxVec3 worldUp = PxVec3(0.f, 1.f, 0.f);

			PxVec3 right = worldUp.cross(dir);
			if (right.magnitudeSquared() < 1e-8f)
				right = PxVec3(1.f, 0.f, 0.f);
			else
				right = right.getNormalized();

			const PxVec3   up = dir.cross(right).getNormalized();
			physx::PxMat33 basis(right, up, dir);
			m_pose.q = physx::PxQuat(basis);
		}

		// 끝점 도달 시 m_pose 캐시 완료 후 done 처리
		if (!m_src.loop && progress >= 1.f)
			m_done = true;

		return m_pose;
	}

	KinematicState CurveKinematicDriver::BuildState() const
	{
		KinematicState state{};
		const float dur = (m_src.duration > 1e-6f) ? m_src.duration : 1.0f;
		const float progress = m_src.loop
			? (m_elapsedTime / dur - std::floor(m_elapsedTime / dur))
			: std::clamp(m_elapsedTime / dur, 0.0f, 1.0f);

		state.phase = static_cast<uint32>(m_src.loop ? (m_elapsedTime / dur) : 0);
		state.t		= progress;
		
		return state;
	}

	void CurveKinematicDriver::BuildArchLengthLUT()
	{
		m_lut.clear();
		m_totalLength = 0.f;

		if (!m_curve) return;

		const std::vector<PxVec3>& nodes = m_curve->GetNodes();
		if (nodes.size() < 2) return;

		m_lut.resize(nodes.size(), 0.f);

		for (size_t i = 1; i < nodes.size(); ++i)
			m_lut[i] = m_lut[i - 1] + (nodes[i] - nodes[i - 1]).magnitude();

		m_totalLength = m_lut.back();
	}

	float CurveKinematicDriver::ArcLengthToT(float arcLen) const
	{
		if (m_lut.size() < 2 || m_totalLength < 1e-7f)
			return 0.f;

		arcLen = physx::PxClamp(arcLen, 0.f, m_totalLength);

		const auto it = std::ranges::lower_bound(m_lut, arcLen);
		if (it == m_lut.end()) return 1.f;

		const size_t i = std::distance(m_lut.begin(), it);
		if (i == 0) return 0.f;

		const float segStart = m_lut[i - 1];
		const float segEnd   = m_lut[i];
		const float segLen   = segEnd - segStart;
		const float localT   = (segLen > 1e-7f) ? ((arcLen - segStart) / segLen) : 0.f;

		const float tPerNode = 1.f / static_cast<float>(m_lut.size() - 1);
		return physx::PxClamp((static_cast<float>(i - 1) + localT) * tPerNode, 0.f, 1.f);
	}





	// -----------------------------------------------------------
	// Orbit
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
			const PxVec3 right  = up.cross(fwd).getNormalized();
			const PxVec3 realUp = fwd.cross(right).getNormalized();
			return MatToQuat(right, realUp, fwd);
		}

	}



	OrbitKinematicDriver::OrbitKinematicDriver(KinematicCommon common, OrbitSource src, TargetPoseResolver resolver)
		: m_common(common), m_src(src), m_angle(src.initialAngleRad), m_resolver(std::move(resolver))
	{
		// planeMode → axis → Gram-Schmidt로 궤도 평면 기저 사전 계산
		m_axisN = AxisFromPlaneMode(m_src.planeMode, m_src.customPlaneNormal);

		const PxVec3 arbitrary = (fabsf(m_axisN.dot({ 0, 1, 0 })) < 0.99f)
			? PxVec3{ 0, 1, 0 }
			: PxVec3{ 1, 0, 0 };

		m_basisR = (arbitrary - m_axisN * m_axisN.dot(arbitrary)).getNormalized();
		m_basisF = m_axisN.cross(m_basisR).getNormalized();

		m_dynamicCenter = m_src.fixedCenter;

		m_pose.p = ComputePosition(m_angle);
		m_pose.q = ComputeRotation(m_angle, m_pose.p);
	}

	PxTransform OrbitKinematicDriver::Tick(float dt)
	{
		if (m_done) return m_pose;

		if (m_src.centerMode == eOrbitCenterMode::FollowTarget
			&& m_src.targetActorId != INVALID_ACTOR_ID
			&& m_resolver)
		{
			if (auto target = m_resolver(m_src.targetActorId); target.has_value())
				m_dynamicCenter = target->p;
		}

		AdvanceAngle(dt);

		// PingPong 끝점 ease: [min,max] → [0,1] 정규화 후 remap
		float renderAngle = m_angle;
		if (m_src.useEaseAtEnds && m_src.endMode == eOrbitEndMode::PingPong)
		{
			const float range = m_src.maxAngleRad - m_src.minAngleRad;
			if (range > 1e-7f)
			{
				const float norm  = (m_angle - m_src.minAngleRad) / range;   // [0,1]
				const float eased = Ease::ApplyProfile(norm, 1.f, m_src.endEaseProfile);
				renderAngle = m_src.minAngleRad + eased * range;
			}
		}

		const PxVec3 pos = ComputePosition(m_angle);

		m_pose.p = pos;
		m_pose.q = ComputeRotation(renderAngle, pos);

		return m_pose;
	}

	KinematicState OrbitKinematicDriver::BuildState() const
	{
		KinematicState state{};

		if (m_src.centerMode == eOrbitCenterMode::FollowTarget)
		{
			state.targetActorId = m_src.targetActorId;
			state.phase    = (m_dir < 0.0f) ? 1u : 0u;
			return state;
		}

		const float range = std::max(1e-6f, m_src.maxAngleRad - m_src.minAngleRad);
		state.t	    = std::clamp((m_angle - m_src.minAngleRad) / range, 0.0f, 1.0f);
		state.phase = (m_dir < 0.0f) ? 1u : 0u;
		
		return state;
	}


	PxVec3 OrbitKinematicDriver::ComputeCenter() const
	{
		switch (m_src.centerMode)
		{
		case eOrbitCenterMode::FixedPoint:    return m_src.fixedCenter;
		case eOrbitCenterMode::FollowTarget:  return m_dynamicCenter + m_src.targetOffset;
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
			dPos = m_basisF * (m_src.ellipseRadius.y * cosA) - m_basisR * (m_src.ellipseRadius.x * sinA);
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
			return m_src.initialRotation;

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

		return m_src.initialRotation;
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
					m_dir	= -1.f;
				}
				else if (m_angle <= m_src.minAngleRad)
				{
					m_angle = m_src.minAngleRad;
					m_dir	= 1.f;
				}
				break;
			}

		case eOrbitEndMode::Clamp:
			{
				m_angle = physx::PxClamp(m_angle + delta, m_src.minAngleRad, m_src.maxAngleRad);
				if (m_angle >= m_src.maxAngleRad || m_angle <= m_src.minAngleRad)
					m_done = true;
				break;
			}
		}
	}




	FollowKinematicDriver::FollowKinematicDriver(KinematicCommon common, FollowSource src, TargetPoseResolver resolver)
		: m_common(common), m_src(std::move(src)), m_resolver(std::move(resolver))
	{
	}

	PxTransform FollowKinematicDriver::Tick(float dt)
	{
		std::optional<PxTransform> targetOpt;
		if (m_resolver) targetOpt = m_resolver(m_src.targetActorId);

		if (!targetOpt.has_value())
		{
			m_targetWasMissing = true;
			return m_pose;
		}

		const PxTransform& targetPose = targetOpt.value();

		const PxVec3 desiredPos = (m_src.offsetSpace == eFollowOffsetSpace::TargetLocal)
			? targetPose.p + targetPose.q.rotate(m_src.offset)
			: targetPose.p + m_src.offset;

		const bool snap = !m_hasValidTarget || (m_targetWasMissing && m_src.snapIfTargetMissing);

		m_hasValidTarget   = true;
		m_targetWasMissing = false;

		if (snap)
		{
			m_pose.p = desiredPos;
			m_pose.q = ComputeTargetRotation(targetPose, desiredPos);
			return m_pose;
		}

		// position follow
		{
			const PxVec3 delta = desiredPos - m_pose.p;
			const float dist = delta.magnitude();

			if (dist > 1e-6f)
			{
				const float step = physx::PxMin(physx::PxMin(m_src.positionFollowSpeed, m_src.maxLinearSpeed) * dt, dist);
				m_pose.p += (delta / dist) * step;
			}
		}

		// rotation follow
		{
			const PxQuat desiredRot = ComputeTargetRotation(targetPose, desiredPos);

			// 현재 → 목표 사이의 회전 각도 (반각 cosine → full angle)
			const float cosHalf = physx::PxAbs(
				m_pose.q.x * desiredRot.x +
				m_pose.q.y * desiredRot.y +
				m_pose.q.z * desiredRot.z +
				m_pose.q.w * desiredRot.w);
			const float fullAngle = 2.f * physx::PxAcos(physx::PxClamp(cosHalf, 0.f, 1.f));

			if (fullAngle > 1e-6f)
			{
				// rotationFollowSpeed와 maxAngularSpeed 양쪽으로 각도 제한
				const float angleStep = physx::PxMin(physx::PxMin(m_src.rotationFollowSpeed, m_src.maxAngularSpeed) * dt, fullAngle);
				const float alpha	  = physx::PxClamp(angleStep / fullAngle, 0.f, 1.f);

				m_pose.q = Ease::Slerp(m_pose.q, desiredRot, alpha);
			}
		}

		return m_pose;
	}

	KinematicState FollowKinematicDriver::BuildState() const
	{
		KinematicState state{};
		state.targetActorId  = m_src.targetActorId;
		state.phase		= m_hasValidTarget ? 1u : 0u;
		
		return state;
	}

	PxQuat FollowKinematicDriver::ComputeTargetRotation(const PxTransform& targetPose, const PxVec3& desiredPos) const
	{
		switch (m_src.rotationMode)
		{
		case eFollowRotationMode::KeepWorldRotation:
			return m_pose.q;

		case eFollowRotationMode::MatchTargetRotation:
			return targetPose.q;

		case eFollowRotationMode::LookAtTarget:
			{
				// desiredPos(offset 적용 위치)에서 target center를 바라봄
				const PxVec3 dir = targetPose.p - desiredPos;
				const float  len = dir.magnitude();
				if (len < 1e-6f) return m_pose.q;
				return BuildLookRotation(dir / len, PxVec3(0.f, 1.f, 0.f));
			}

		case eFollowRotationMode::OrientAlongVelocity:
			{
				// 현재 pose에서 목표 위치를 향하는 방향을 속도 방향으로 사용
				const PxVec3 vel = desiredPos - m_pose.p;
				const float  len = vel.magnitude();
				if (len < 1e-6f) return m_pose.q;
				return BuildLookRotation(vel / len, PxVec3(0.f, 1.f, 0.f));
			}
		}

		return m_pose.q;
	}

	NetworkPoseKinematicDriver::NetworkPoseKinematicDriver(KinematicCommon common, NetworkPoseSource src)
		: m_common(common), m_src(src)
	{
	}


} // namespace jam::px
