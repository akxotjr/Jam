#pragma once


#include "jamnet/runtime/protocol/codec/BitBuffer.h"

#include <jampx/PhysicsTypes.h>


namespace jam::net
{
	inline float WrapPi(float a);
	inline float WrapPiHalf(float a);

	struct ReplicationConfig
	{
		// 월드 AABB (스냅샷 UNORM 기준)
		static constexpr float MIN_WORLD_X = -5000.f;
		static constexpr float MIN_WORLD_Y = -5000.f;
		static constexpr float MIN_WORLD_Z = -5000.f;
		static constexpr float MAX_WORLD_X = 5000.f;
		static constexpr float MAX_WORLD_Y = 5000.f;
		static constexpr float MAX_WORLD_Z = 5000.f;

		static constexpr float MAX_YAW = px::PI;
		static constexpr float MIN_YAW = -px::PI;
		static constexpr float MAX_YAW_RATE = px::PI;
		static constexpr float MIN_YAW_RATE = -px::PI;
		static constexpr float MAX_SPEED = 512.f;


		// --- FullPacked(192bit) 설정 ---
		// 위치 정밀도: bits가 클수록 오차 감소. 20bit면 10km폭(=10000)에서 약 1cm급.
		static constexpr int32   POS_BITS = 20;

		// 선형/각속도 magnitude 범위 (각 게임에서 조정 가능)
		static constexpr float MAX_LIN_SPEED = 512.f;        // m/s
		static constexpr float MAX_ANG_SPEED = px::PI * 16.f;  // rad/s (초당 8회전 정도)

		// velocity direction angles bits
		static constexpr int32 VEL_THETA_BITS = 12; // [-pi, +pi]
		static constexpr int32 VEL_PHI_BITS = 12;   // [-pi/2, +pi/2]
		static constexpr int32 VEL_MAG_BITS = 18;   // [0, max]

		static constexpr float DELTA_POS_RANGE = 16.f;
		static constexpr int32 DELTA_POS_BITS = 12;
	};

	struct CharacterReplicationConfig
	{
		// Full (160 bits)

		static constexpr int32 POS_BITS = ReplicationConfig::POS_BITS;
		static constexpr int32 YAW_BITS = 16;
		static constexpr int32 PITCH_BITS = 16;
		static constexpr int32 VY_BITS = 12;
		static constexpr float VY_ABS_MAX = 64.f;

		static constexpr int32 SPEED_BITS = 10;
		static constexpr float SPEED_MAX = ReplicationConfig::MAX_LIN_SPEED;

		static constexpr int32 MOVE_DIR_BITS = 8;
		static constexpr int32 FLAGS_BITS_FULL160 = 14;
		static constexpr int32 FLAGS_BITS_DELTA128 = 18;

		// Delta (128 bits)

		static constexpr float DELTA_POS_RANGE = ReplicationConfig::DELTA_POS_RANGE;
		static constexpr int32 DELTA_POS_BITS = ReplicationConfig::DELTA_POS_BITS;

		static constexpr int32 DELTA_YAW_BITS = 12;
		static constexpr int32 DELTA_PITCH_BITS = 12;

		static constexpr float DELTA_YAW_ABS_MAX = px::PI;
		static constexpr float DELTA_PITCH_ABS_MAX = px::PI_DIV_TWO;

	};

	inline uint64 ExtractBits64(uint64 v, int32 bitOffset, int32 bitCount)
	{
		if (bitCount <= 0)  return 0;
		if (bitCount >= 64) return v >> bitOffset;
		const uint64 mask = (1ull << bitCount) - 1ull;
		return (v >> bitOffset) & mask;
	}

	inline float ClampFloat(float v, float lo, float hi)
	{
		return (v < lo) ? lo : (v > hi ? hi : v);
	}

	template <uint32 MaxQ>
	inline uint32 EncodeUNormFast(float v, float lo, float scaleToQ)
	{
		const float q = (v - lo) * scaleToQ + 0.5f;
		const float clamped = ClampFloat(q, 0.0f, static_cast<float>(MaxQ));
		return static_cast<uint32>(clamped);
	}

	template <uint32 MaxQ>
	inline float DecodeUNormFast(uint32 q, float lo, float scaleFromQ)
	{
		return lo + static_cast<float>(q) * scaleFromQ;
	}

	template <uint32 MaxQ>
	inline uint32 EncodeSNormFast(float v, float absMax, float scaleToQ)
	{
		const float clamped = ClampFloat(v, -absMax, absMax);
		const float q = (clamped + absMax) * scaleToQ + 0.5f;
		return static_cast<uint32>(ClampFloat(q, 0.0f, static_cast<float>(MaxQ)));
	}

	template <uint32 MaxQ>
	inline float DecodeSNormFast(uint32 q, float absMax, float scaleFromQ)
	{
		return static_cast<float>(q) * scaleFromQ - absMax;
	}

	inline uint32 EncodeUNorm(float v, float lo, float hi, int bits)
	{
		v = ClampFloat(v, lo, hi);
		const float t = (v - lo) / (hi - lo);
		const float q = t * static_cast<float>((1u << bits) - 1u) + 0.5f;
		return static_cast<uint32>(q);
	}

	inline float DecodeUNorm(uint32 q, float lo, float hi, int bits)
	{
		const float t = static_cast<float>(q) / static_cast<float>((1u << bits) - 1u);
		return lo + t * (hi - lo);
	}

	// SNorm은 [-absMax, +absMax]를 [0..(2^bits-1)]로 매핑
	inline uint32 EncodeSNorm(float v, float absMax, int32 bits)
	{
		if (absMax <= 0.f) return 0;
		v = ClampFloat(v, -absMax, absMax);
		const float t = (v / absMax) * 0.5f + 0.5f;
		const float q = t * static_cast<float>((1u << bits) - 1u) + 0.5f;
		return static_cast<uint32>(q);
	}

	inline float DecodeSNorm(uint32 q, float absMax, int32 bits)
	{
		if (absMax <= 0.f) return 0.f;
		const float t  = static_cast<float>(q) / static_cast<float>((1u << bits) - 1u);
		const float sn = t * 2.f - 1.f;
		return sn * absMax;
	}


	namespace detail
	{
		static constexpr uint32 kWorldPosMaxQ			= (1u << ReplicationConfig::POS_BITS) - 1u;
		static constexpr uint32 kDeltaPosMaxQ			= (1u << ReplicationConfig::DELTA_POS_BITS) - 1u;
		static constexpr uint32 kYaw16MaxQ				= (1u << CharacterReplicationConfig::YAW_BITS) - 1u;
		static constexpr uint32 kPitch16MaxQ			= (1u << CharacterReplicationConfig::PITCH_BITS) - 1u;
		static constexpr uint32 kSpeed10MaxQ			= (1u << CharacterReplicationConfig::SPEED_BITS) - 1u;
		static constexpr uint32 kVy12MaxQ				= (1u << CharacterReplicationConfig::VY_BITS) - 1u;
		static constexpr uint32 kMoveDir8MaxQ			= (1u << CharacterReplicationConfig::MOVE_DIR_BITS) - 1u;
		static constexpr uint32 kDeltaYaw12MaxQ			= (1u << CharacterReplicationConfig::DELTA_YAW_BITS) - 1u;
		static constexpr uint32 kDeltaPitch12MaxQ		= (1u << CharacterReplicationConfig::DELTA_PITCH_BITS) - 1u;
		static constexpr uint32 kQuat15MaxQ				= (1u << 15) - 1u;

		static constexpr float kWorldPosXScaleToQ		= static_cast<float>(kWorldPosMaxQ) / (ReplicationConfig::MAX_WORLD_X - ReplicationConfig::MIN_WORLD_X);
		static constexpr float kWorldPosYScaleToQ		= static_cast<float>(kWorldPosMaxQ) / (ReplicationConfig::MAX_WORLD_Y - ReplicationConfig::MIN_WORLD_Y);
		static constexpr float kWorldPosZScaleToQ		= static_cast<float>(kWorldPosMaxQ) / (ReplicationConfig::MAX_WORLD_Z - ReplicationConfig::MIN_WORLD_Z);
		static constexpr float kWorldPosXScaleFromQ		= (ReplicationConfig::MAX_WORLD_X - ReplicationConfig::MIN_WORLD_X) / static_cast<float>(kWorldPosMaxQ);
		static constexpr float kWorldPosYScaleFromQ		= (ReplicationConfig::MAX_WORLD_Y - ReplicationConfig::MIN_WORLD_Y) / static_cast<float>(kWorldPosMaxQ);
		static constexpr float kWorldPosZScaleFromQ		= (ReplicationConfig::MAX_WORLD_Z - ReplicationConfig::MIN_WORLD_Z) / static_cast<float>(kWorldPosMaxQ);
		static constexpr float kDeltaPosScaleToQ		= static_cast<float>(kDeltaPosMaxQ) / (ReplicationConfig::DELTA_POS_RANGE * 2.0f);
		static constexpr float kDeltaPosScaleFromQ		= (ReplicationConfig::DELTA_POS_RANGE * 2.0f) / static_cast<float>(kDeltaPosMaxQ);
		static constexpr float kYaw16ScaleToQ			= static_cast<float>(kYaw16MaxQ) / (px::TWO_PI);
		static constexpr float kYaw16ScaleFromQ			= (px::TWO_PI) / static_cast<float>(kYaw16MaxQ);
		static constexpr float kPitch16ScaleToQ			= static_cast<float>(kPitch16MaxQ) / (px::PI);
		static constexpr float kPitch16ScaleFromQ		= (px::PI) / static_cast<float>(kPitch16MaxQ);
		static constexpr float kSpeed10ScaleToQ			= static_cast<float>(kSpeed10MaxQ) / CharacterReplicationConfig::SPEED_MAX;
		static constexpr float kSpeed10ScaleFromQ		= CharacterReplicationConfig::SPEED_MAX / static_cast<float>(kSpeed10MaxQ);
		static constexpr float kVy12ScaleToQ			= static_cast<float>(kVy12MaxQ) / (CharacterReplicationConfig::VY_ABS_MAX * 2.0f);
		static constexpr float kVy12ScaleFromQ			= (CharacterReplicationConfig::VY_ABS_MAX * 2.0f) / static_cast<float>(kVy12MaxQ);
		static constexpr float kMoveDir8ScaleToQ		= static_cast<float>(kMoveDir8MaxQ) * 0.5f;
		static constexpr float kMoveDir8ScaleFromQ		= 2.0f / static_cast<float>(kMoveDir8MaxQ);
		static constexpr float kDeltaYaw12ScaleToQ		= static_cast<float>(kDeltaYaw12MaxQ) / (CharacterReplicationConfig::DELTA_YAW_ABS_MAX * 2.0f);
		static constexpr float kDeltaYaw12ScaleFromQ	= (CharacterReplicationConfig::DELTA_YAW_ABS_MAX * 2.0f) / static_cast<float>(kDeltaYaw12MaxQ);
		static constexpr float kDeltaPitch12ScaleToQ	= static_cast<float>(kDeltaPitch12MaxQ) / (CharacterReplicationConfig::DELTA_PITCH_ABS_MAX * 2.0f);
		static constexpr float kDeltaPitch12ScaleFromQ	= (CharacterReplicationConfig::DELTA_PITCH_ABS_MAX * 2.0f) / static_cast<float>(kDeltaPitch12MaxQ);
		static constexpr float kQuat15Range				= 0.7071067811865475f;
		static constexpr float kQuat15ScaleToQ			= static_cast<float>(kQuat15MaxQ) / (kQuat15Range * 2.0f);
		static constexpr float kQuat15ScaleFromQ		= (kQuat15Range * 2.0f) / static_cast<float>(kQuat15MaxQ);

		inline uint32 EncodeWorldPosX20(float v)			{ return EncodeUNormFast<kWorldPosMaxQ>(ClampFloat(v, ReplicationConfig::MIN_WORLD_X, ReplicationConfig::MAX_WORLD_X), ReplicationConfig::MIN_WORLD_X, kWorldPosXScaleToQ); }
		inline uint32 EncodeWorldPosY20(float v)			{ return EncodeUNormFast<kWorldPosMaxQ>(ClampFloat(v, ReplicationConfig::MIN_WORLD_Y, ReplicationConfig::MAX_WORLD_Y), ReplicationConfig::MIN_WORLD_Y, kWorldPosYScaleToQ); }
		inline uint32 EncodeWorldPosZ20(float v)			{ return EncodeUNormFast<kWorldPosMaxQ>(ClampFloat(v, ReplicationConfig::MIN_WORLD_Z, ReplicationConfig::MAX_WORLD_Z), ReplicationConfig::MIN_WORLD_Z, kWorldPosZScaleToQ); }
		inline float  DecodeWorldPosX20(uint32 q)			{ return DecodeUNormFast<kWorldPosMaxQ>(q, ReplicationConfig::MIN_WORLD_X, kWorldPosXScaleFromQ); }
		inline float  DecodeWorldPosY20(uint32 q)			{ return DecodeUNormFast<kWorldPosMaxQ>(q, ReplicationConfig::MIN_WORLD_Y, kWorldPosYScaleFromQ); }
		inline float  DecodeWorldPosZ20(uint32 q)			{ return DecodeUNormFast<kWorldPosMaxQ>(q, ReplicationConfig::MIN_WORLD_Z, kWorldPosZScaleFromQ); }
		inline uint32 EncodeDeltaPos12(float v)				{ return EncodeSNormFast<kDeltaPosMaxQ>(v, ReplicationConfig::DELTA_POS_RANGE, kDeltaPosScaleToQ); }
		inline float  DecodeDeltaPos12(uint32 q)			{ return DecodeSNormFast<kDeltaPosMaxQ>(q, ReplicationConfig::DELTA_POS_RANGE, kDeltaPosScaleFromQ); }
		inline uint32 EncodeAngleYaw16Fast(float yaw)		{ return EncodeUNormFast<kYaw16MaxQ>(WrapPi(yaw), -px::PI, kYaw16ScaleToQ); }
		inline float  DecodeAngleYaw16Fast(uint32 q)		{ return DecodeUNormFast<kYaw16MaxQ>(q, -px::PI, kYaw16ScaleFromQ); }
		inline uint32 EncodeAnglePitch16Fast(float pitch)	{ return EncodeUNormFast<kPitch16MaxQ>(WrapPiHalf(pitch), -px::PI_DIV_TWO, kPitch16ScaleToQ); }
		inline float  DecodeAnglePitch16Fast(uint32 q)		{ return DecodeUNormFast<kPitch16MaxQ>(q, -px::PI_DIV_TWO, kPitch16ScaleFromQ); }
		inline uint32 EncodeSpeed10Fast(float speed)		{ return EncodeUNormFast<kSpeed10MaxQ>(ClampFloat(speed, 0.0f, CharacterReplicationConfig::SPEED_MAX), 0.0f, kSpeed10ScaleToQ); }
		inline float  DecodeSpeed10Fast(uint32 q)			{ return DecodeUNormFast<kSpeed10MaxQ>(q, 0.0f, kSpeed10ScaleFromQ); }
		inline uint32 EncodeVY12Fast(float vy)				{ return EncodeSNormFast<kVy12MaxQ>(vy, CharacterReplicationConfig::VY_ABS_MAX, kVy12ScaleToQ); }
		inline float  DecodeVY12Fast(uint32 q)				{ return DecodeSNormFast<kVy12MaxQ>(q, CharacterReplicationConfig::VY_ABS_MAX, kVy12ScaleFromQ); }
		inline uint32 EncodeMoveDir8Fast(float v)			{ return EncodeSNormFast<kMoveDir8MaxQ>(v, 1.0f, kMoveDir8ScaleToQ); }
		inline float  DecodeMoveDir8Fast(uint32 q)			{ return DecodeSNormFast<kMoveDir8MaxQ>(q, 1.0f, kMoveDir8ScaleFromQ); }
		inline uint32 EncodeDeltaYaw12Fast(float yaw)		{ return EncodeSNormFast<kDeltaYaw12MaxQ>(yaw, CharacterReplicationConfig::DELTA_YAW_ABS_MAX, kDeltaYaw12ScaleToQ); }
		inline float  DecodeDeltaYaw12Fast(uint32 q)		{ return DecodeSNormFast<kDeltaYaw12MaxQ>(q, CharacterReplicationConfig::DELTA_YAW_ABS_MAX, kDeltaYaw12ScaleFromQ); }
		inline uint32 EncodeDeltaPitch12Fast(float pitch)	{ return EncodeSNormFast<kDeltaPitch12MaxQ>(pitch, CharacterReplicationConfig::DELTA_PITCH_ABS_MAX, kDeltaPitch12ScaleToQ); }
		inline float  DecodeDeltaPitch12Fast(uint32 q)		{ return DecodeSNormFast<kDeltaPitch12MaxQ>(q, CharacterReplicationConfig::DELTA_PITCH_ABS_MAX, kDeltaPitch12ScaleFromQ); }
		inline uint32 EncodeQuatSNorm15Fast(float v)		{ return EncodeSNormFast<kQuat15MaxQ>(v, kQuat15Range, kQuat15ScaleToQ); }
		inline float  DecodeQuatSNorm15Fast(uint32 q)		{ return DecodeSNormFast<kQuat15MaxQ>(q, kQuat15Range, kQuat15ScaleFromQ); }
	}

	inline float WrapPi(float a)
	{
		a = std::fmod(a + px::PI, px::TWO_PI);
		if (a < 0.f) a += px::TWO_PI;
		return a - px::PI;
	}

	inline float WrapPiHalf(float a)
	{
		// clamp pitch after wrap (pitch typically limited)
		// Note: pitch usually doesn't need wrapping; use clamp for safety.
		return ClampFloat(a, -px::PI_DIV_TWO, px::PI_DIV_TWO);
	}


	// ------------------------------------------------------------
	// Quaternion compression: 48bit
	// - largest index (2bit)
	// - sign of largest component (1bit) : if negative, negate all for canonical form
	// - remaining 3 components: 15bit SNorm each in range [-1/sqrt(2), +1/sqrt(2)]
	// ------------------------------------------------------------
	struct PackedQuat48
	{
		uint64 data = 0; // 48bit used
	};


	// Assume normalized
	inline PackedQuat48 PackQuat48(const px::Quat& q)
	{
		JAM_ASSERT_MSG(q.IsFinite(), "PackQuat48() expects finite quaternion");

		float c[4] = { q.x, q.y, q.z, q.w };

		int largest = 0;
		float maxAbs = fabsf(c[0]);
		for (int i = 1; i < 4; ++i)
		{
			const float a = fabsf(c[i]);
			if (a > maxAbs)
			{
				maxAbs = a;
				largest = i;
			}
		}

		bool sign = (c[largest] < 0.f);
		if (sign)
		{
			for (float& v : c) v = -v;
		}

		float a0 = 0.f, a1 = 0.f, a2 = 0.f;
		int outIdx = 0;
		for (int i = 0; i < 4; ++i)
		{
			if (i == largest) continue;

			if (outIdx == 0) a0 = c[i];
			else if (outIdx == 1) a1 = c[i];
			else a2 = c[i];
			++outIdx;
		}

		const uint64 qa = detail::EncodeQuatSNorm15Fast(a0);
		const uint64 qb = detail::EncodeQuatSNorm15Fast(a1);
		const uint64 qc = detail::EncodeQuatSNorm15Fast(a2);

		PackedQuat48 out{};
		// layout: [0..1]=largest, [2]=sign, [3..17]=qa, [18..32]=qb, [33..47]=qc
		out.data |= (static_cast<uint64>(largest) & 0x3ull) << 0;
		out.data |= (static_cast<uint64>(sign ? 1u : 0u) & 0x1ull) << 2;
		out.data |= (qa & 0x7FFFull) << 3;
		out.data |= (qb & 0x7FFFull) << 18;
		out.data |= (qc & 0x7FFFull) << 33;

		return out;
	}

	inline px::Quat UnpackQuat48(PackedQuat48 packed)
	{
		const int largest = static_cast<int>((packed.data >> 0) & 0x3ull);
		const bool sign = (((packed.data >> 2) & 0x1ull) != 0);

		const uint32 qa = static_cast<uint32>((packed.data >> 3) & 0x7FFFull);
		const uint32 qb = static_cast<uint32>((packed.data >> 18) & 0x7FFFull);
		const uint32 qc = static_cast<uint32>((packed.data >> 33) & 0x7FFFull);

		const float a0 = detail::DecodeQuatSNorm15Fast(qa);
		const float a1 = detail::DecodeQuatSNorm15Fast(qb);
		const float a2 = detail::DecodeQuatSNorm15Fast(qc);

		float c[4] = { 0.f, 0.f, 0.f, 0.f };

		// place a0,a1,a2 into the non-largest slots (x,y,z,w skipping largest)
		int inIdx = 0;
		for (int i = 0; i < 4; ++i)
		{
			if (i == largest) continue;
			if (inIdx == 0) c[i] = a0;
			else if (inIdx == 1) c[i] = a1;
			else c[i] = a2;
			++inIdx;
		}

		// reconstruct largest (positive)
		float sum = 0.f;
		for (int i = 0; i < 4; ++i)
		{
			if (i == largest) continue;
			sum += c[i] * c[i];
		}

		float largestV = sqrtf(fmaxf(0.f, 1.f - sum));
		c[largest] = largestV;

		// restore original sign: if sign bit was set, negate all
		if (sign)
		{
			for (float& v : c) v = -v;
		}

		px::Quat q(c[0], c[1], c[2], c[3]);
		q.Normalize();
		return q;
	}

	// ------------------------------------------------------------
	// Velocity: (theta, phi, mag) = 42bit
	// theta: [-pi, +pi] (12bit)
	// phi  : [-pi/2, +pi/2] (12bit)
	// mag  : [0, maxMag] (18bit)
	// ------------------------------------------------------------
	struct PackedVel42
	{
		uint64 data = 0; // 42-bit used
	};

	inline PackedVel42 PackVel42(const px::Vec3& v, float maxMag)
	{
		PackedVel42 out{};

		const float mag = v.Magnitude();
		if (mag <= 1e-6f)
		{
			// 방향은 0으로 고정, mag=0
			out.data = 0;
			return out;
		}

		const px::Vec3 dir  = v / mag;
		const float theta = std::atan2(dir.x, dir.z);              // [-pi, +pi]
		const float phi   = asinf(ClampFloat(dir.y, -1.f, 1.f));     // [-pi/2, +pi/2]

		const uint64 t = EncodeUNorm(theta, -px::PI, px::PI, ReplicationConfig::VEL_THETA_BITS);
		const uint64 p = EncodeUNorm(phi, -px::PI_DIV_TWO, px::PI_DIV_TWO, ReplicationConfig::VEL_PHI_BITS);
		const uint64 m = EncodeUNorm(mag, 0.f, maxMag, ReplicationConfig::VEL_MAG_BITS);

		// layout: [0..11]=theta, [12..23]=phi, [24..41]=mag
		out.data |= (t & ((1ull << ReplicationConfig::VEL_THETA_BITS) - 1ull)) << 0;
		out.data |= (p & ((1ull << ReplicationConfig::VEL_PHI_BITS) - 1ull)) << ReplicationConfig::VEL_THETA_BITS;
		out.data |= (m & ((1ull << ReplicationConfig::VEL_MAG_BITS) - 1ull)) << (ReplicationConfig::VEL_THETA_BITS + ReplicationConfig::VEL_PHI_BITS);

		return out;
	}

	inline px::Vec3 UnpackVel42(PackedVel42 packed, float maxMag)
	{
		const uint64 thetaQ = ExtractBits64(packed.data, 0, ReplicationConfig::VEL_THETA_BITS);
		const uint64 phiQ   = ExtractBits64(packed.data, ReplicationConfig::VEL_THETA_BITS, ReplicationConfig::VEL_PHI_BITS);
		const uint64 magQ   = ExtractBits64(packed.data, ReplicationConfig::VEL_THETA_BITS + ReplicationConfig::VEL_PHI_BITS, ReplicationConfig::VEL_MAG_BITS);

		const float theta = DecodeUNorm(static_cast<uint32>(thetaQ), -px::PI, px::PI, ReplicationConfig::VEL_THETA_BITS);
		const float phi   = DecodeUNorm(static_cast<uint32>(phiQ), -px::PI_DIV_TWO, px::PI_DIV_TWO, ReplicationConfig::VEL_PHI_BITS);
		const float mag   = DecodeUNorm(static_cast<uint32>(magQ), 0.f, maxMag, ReplicationConfig::VEL_MAG_BITS);

		if (mag <= 1e-6f)
			return px::Vec3();

		const float cosPhi   = cosf(phi);
		const float sinPhi   = sinf(phi);
		const float sinTheta = sinf(theta);
		const float cosTheta = cosf(theta);

		// dir.x = sin(theta)*cos(phi), dir.y = sin(phi), dir.z = cos(theta)*cos(phi)
		const px::Vec3 dir(sinTheta * cosPhi, sinPhi, cosTheta * cosPhi);
		return dir * mag;
	}




	/**
	 * @brief Rigid Full 192 (3 x u64 = 192 container)
	 * 
	 *  - 192 = 60 + 48 + 42 + 42                   
	 *  - Layout (bit stream, LSB-first):           
	 *      - pos               (60 = 20 + 20 + 20) 
	 *      - rot               (48 = quat48)       
	 *      - linear velocity   (42 = vel42)        
	 *      - angular velocity  (42 = vel42)        
	 */
	using PackedRigidFull192 = BitBuffer<uint64, 3>;


	inline bool PackRigidFull192(const px::RigidState& rs, OUT PackedRigidFull192& packed)
	{
		JAM_ASSERT_MSG(rs.IsFinite(), "PackRigidFull192() expects validated finite authoritative state");

		packed.Clear();

		const uint64        px = detail::EncodeWorldPosX20(rs.pose.p.x);
		const uint64        py = detail::EncodeWorldPosY20(rs.pose.p.y);
		const uint64        pz = detail::EncodeWorldPosZ20(rs.pose.p.z);
		const PackedQuat48  q  = PackQuat48(rs.pose.q);
		const PackedVel42   lv = PackVel42(rs.linVel, ReplicationConfig::MAX_LIN_SPEED);
		const PackedVel42   av = PackVel42(rs.angVel, ReplicationConfig::MAX_ANG_SPEED);

		if (!packed.WriteBits(px, ReplicationConfig::POS_BITS)
			|| !packed.WriteBits(py, ReplicationConfig::POS_BITS)
			|| !packed.WriteBits(pz, ReplicationConfig::POS_BITS)
			|| !packed.WriteBits(q.data, 48)
			|| !packed.WriteBits(lv.data, 42)
			|| !packed.WriteBits(av.data, 42))
		{
			return false;
		}

		return (packed.Cursor() == 192);
	}

	inline bool UnpackRigidFull192(uint64 packed0, uint64 packed1, uint64 packed2, OUT px::RigidState& state)
	{
		BitBuffer<uint64, 3> buffer(std::array<uint64, 3>{ packed0, packed1, packed2 });
		uint64 pxQ64 = 0;
		uint64 pyQ64 = 0;
		uint64 pzQ64 = 0;

		PackedQuat48 q{};
		PackedVel42 lv{};
		PackedVel42 av{};
		if (!buffer.ReadBits(ReplicationConfig::POS_BITS, pxQ64)
			|| !buffer.ReadBits(ReplicationConfig::POS_BITS, pyQ64)
			|| !buffer.ReadBits(ReplicationConfig::POS_BITS, pzQ64)
			|| !buffer.ReadBits(48, q.data)
			|| !buffer.ReadBits(42, lv.data)
			|| !buffer.ReadBits(42, av.data))
		{
			return false;
		}

		const uint32 pxQ = static_cast<uint32>(pxQ64);
		const uint32 pyQ = static_cast<uint32>(pyQ64);
		const uint32 pzQ = static_cast<uint32>(pzQ64);

		state.pose.p.x = detail::DecodeWorldPosX20(pxQ);
		state.pose.p.y = detail::DecodeWorldPosY20(pyQ);
		state.pose.p.z = detail::DecodeWorldPosZ20(pzQ);
		state.pose.q   = UnpackQuat48(q);
		state.linVel   = UnpackVel42(lv, ReplicationConfig::MAX_LIN_SPEED);
		state.angVel   = UnpackVel42(av, ReplicationConfig::MAX_ANG_SPEED);

		JAM_ASSERT_MSG(state.IsFinite(), "UnpackRigidFull192() produced non-finite state");

		return (buffer.Cursor() == 192);
	}

	/**
	 * @note Rigid Delta 128 (2 x u64 = 120 container)
	 *
	 *  - 128 = 36 + 48 + 42 + 2
	 *  - Layout (bit stream, LSB-first):
	 *      - delta pos         (36 = 12 + 12 + 12)
	 *      - delta rot         (48 = quat48)
	 *      - linear velocity   (42 = vel42)
	 *      - blank             (2)
	 */

	using PackedRigidDelta128 = BitBuffer<uint64, 2>;

	inline bool PackRigidDelta128(const px::Vec3& baselinePos, const px::Quat& baselineRot, const px::RigidState& state, OUT PackedRigidDelta128& packed)
	{
		JAM_ASSERT_MSG(baselinePos.IsFinite(), "PackRigidDelta128() expects finite baseline position");
		JAM_ASSERT_MSG(baselineRot.IsFinite(), "PackRigidDelta128() expects finite baseline rotation");
		JAM_ASSERT_MSG(state.IsFinite()      , "PackRigidDelta128() expects validated finite authoritative state");

		packed.Clear();

		const px::Vec3 dp = state.pose.p - baselinePos;

		if (fabsf(dp.x) > ReplicationConfig::DELTA_POS_RANGE ||
			fabsf(dp.y) > ReplicationConfig::DELTA_POS_RANGE ||
			fabsf(dp.z) > ReplicationConfig::DELTA_POS_RANGE)
		{
			return false;
		}

		// relative rotation: dr = inv(baseline) * current
		const px::Quat invBr = baselineRot.Conjugate();
		const px::Quat dr    = invBr * state.pose.q;

		const uint64        dx  = detail::EncodeDeltaPos12(dp.x);
		const uint64        dy  = detail::EncodeDeltaPos12(dp.y);
		const uint64        dz  = detail::EncodeDeltaPos12(dp.z);
		const PackedQuat48  q   = PackQuat48(dr);
		const PackedVel42   lv  = PackVel42(state.linVel, ReplicationConfig::MAX_LIN_SPEED);

		constexpr uint64    blank = 0;

		if (!packed.WriteBits(dx, ReplicationConfig::DELTA_POS_BITS)
			|| !packed.WriteBits(dy, ReplicationConfig::DELTA_POS_BITS)
			|| !packed.WriteBits(dz, ReplicationConfig::DELTA_POS_BITS)
			|| !packed.WriteBits(q.data, 48)
			|| !packed.WriteBits(lv.data, 42)
			|| !packed.WriteBits(blank, 2))
		{
			return false;
		}

		return (packed.Cursor() == 128);
	}


	inline bool UnpackRigidDelta128(const px::Vec3& baselinePos, const px::Quat baselineRot, uint64 data0, uint64 data1, OUT px::RigidState& state)
	{
		JAM_ASSERT_MSG(baselinePos.IsFinite(), "UnpackRigidDelta128() expects finite baseline position");
		JAM_ASSERT_MSG(baselineRot.IsFinite(), "UnpackRigidDelta128() expects finite baseline rotation");

		BitBuffer<uint64, 2> buffer(std::array<uint64, 2>{ data0, data1 });
		uint64 dxQ64 = 0;
		uint64 dyQ64 = 0;
		uint64 dzQ64 = 0;

		PackedQuat48 q{};
		PackedVel42 lv{};
		uint64 blank = 0;
		if (!buffer.ReadBits(ReplicationConfig::DELTA_POS_BITS, dxQ64)
			|| !buffer.ReadBits(ReplicationConfig::DELTA_POS_BITS, dyQ64)
			|| !buffer.ReadBits(ReplicationConfig::DELTA_POS_BITS, dzQ64)
			|| !buffer.ReadBits(48, q.data)
			|| !buffer.ReadBits(42, lv.data)
			|| !buffer.ReadBits(2, blank))
		{
			return false;
		}

		const uint32 dxQ = static_cast<uint32>(dxQ64);
		const uint32 dyQ = static_cast<uint32>(dyQ64);
		const uint32 dzQ = static_cast<uint32>(dzQ64);

		const float dx = detail::DecodeDeltaPos12(dxQ);
		const float dy = detail::DecodeDeltaPos12(dyQ);
		const float dz = detail::DecodeDeltaPos12(dzQ);

		const px::Vec3 dp(dx, dy, dz);

		const px::Quat dr = UnpackQuat48(q);
		px::Quat r = baselineRot * dr;
		r.Normalize();

		state.pose.p = baselinePos + dp;
		state.pose.q = r;
		state.linVel = UnpackVel42(lv, ReplicationConfig::MAX_LIN_SPEED);

		JAM_ASSERT_MSG(state.IsFinite(), "UnpackRigidDelta128() produced non-finite state");

		return (buffer.Cursor() == 128);
	}



	/**
	 * @note Character Full 160 (2 x u64 + u32 = 160 container)
	 *
	 *  -Layout (bit stream, LSB-first):
	 *      - pos               (60 = 20 + 20 + 20)
	 *      - bodyYaw           (16)
	 *      - viewYaw           (16)
	 *      - viewPitch         (16)
	 *      - verticalSpeed     (12)
	 *      - horizontalSpeed   (10)
	 *      - moveDir           (16 = 8 + 8)
	 *      - stateFlags        (14)
	 */


	using PackedCharacterFull160 = BitBuffer<uint32, 5>;

	inline bool PackCharacterFull160(const px::CharacterState& state, OUT PackedCharacterFull160& packed)
	{
		JAM_ASSERT_MSG(state.IsFinite(), "PackCharacterFull160() expects validated finite authoritative state");

		packed.Clear();

		const uint64 px     = static_cast<uint64>(detail::EncodeWorldPosX20(state.pos.x));
		const uint64 py     = static_cast<uint64>(detail::EncodeWorldPosY20(state.pos.y));
		const uint64 pz     = static_cast<uint64>(detail::EncodeWorldPosZ20(state.pos.z));
		const uint64 bodyYawQ = static_cast<uint64>(detail::EncodeAngleYaw16Fast(state.bodyYaw));
		const uint64 yawQ   = static_cast<uint64>(detail::EncodeAngleYaw16Fast(state.viewYaw));
		const uint64 pitchQ = static_cast<uint64>(detail::EncodeAnglePitch16Fast(state.viewPitch));
		const uint64 vyQ    = static_cast<uint64>(detail::EncodeVY12Fast(state.verticalSpeed));
		const uint64 spdQ   = static_cast<uint64>(detail::EncodeSpeed10Fast(state.horizontalSpeed));
		const uint64 mxQ    = static_cast<uint64>(detail::EncodeMoveDir8Fast(state.moveDir.x));
		const uint64 myQ    = static_cast<uint64>(detail::EncodeMoveDir8Fast(state.moveDir.y));
		const uint64 flgQ   = static_cast<uint64>(state.stateFlags) & ((1ull << CharacterReplicationConfig::FLAGS_BITS_FULL160) - 1ull);

		if (!packed.WriteBits(px, CharacterReplicationConfig::POS_BITS)
			|| !packed.WriteBits(py,     CharacterReplicationConfig::POS_BITS)
			|| !packed.WriteBits(pz,     CharacterReplicationConfig::POS_BITS)
			|| !packed.WriteBits(bodyYawQ, CharacterReplicationConfig::YAW_BITS)
			|| !packed.WriteBits(yawQ,   CharacterReplicationConfig::YAW_BITS)
			|| !packed.WriteBits(pitchQ, CharacterReplicationConfig::PITCH_BITS)
			|| !packed.WriteBits(vyQ,    CharacterReplicationConfig::VY_BITS)
			|| !packed.WriteBits(spdQ,   CharacterReplicationConfig::SPEED_BITS)
			|| !packed.WriteBits(mxQ,    CharacterReplicationConfig::MOVE_DIR_BITS)
			|| !packed.WriteBits(myQ,    CharacterReplicationConfig::MOVE_DIR_BITS)
			|| !packed.WriteBits(flgQ,   CharacterReplicationConfig::FLAGS_BITS_FULL160))
		{
			return false;
		}

		return (packed.Cursor() == 160);
	}

	inline bool UnpackCharacterFull160(uint32 packed0, uint32 packed1, uint32 packed2, uint32 packed3, uint32 packed4, OUT px::CharacterState& state)
	{
		PackedCharacterFull160 packed(std::array<uint32, 5>{ packed0, packed1, packed2, packed3, packed4 });
		packed.ResetCursor();
		uint64 pxQ64 = 0;
		uint64 pyQ64 = 0;
		uint64 pzQ64 = 0;
		uint64 bodyYawQ64 = 0;
		uint64 yawQ64 = 0;
		uint64 pitchQ64 = 0;
		uint64 vyQ64 = 0;
		uint64 spdQ64 = 0;
		uint64 mxQ64 = 0;
		uint64 myQ64 = 0;
		uint64 flgQ64 = 0;
		if (!packed.ReadBits(CharacterReplicationConfig::POS_BITS, pxQ64)
			|| !packed.ReadBits(CharacterReplicationConfig::POS_BITS, pyQ64)
			|| !packed.ReadBits(CharacterReplicationConfig::POS_BITS, pzQ64)
			|| !packed.ReadBits(CharacterReplicationConfig::YAW_BITS, bodyYawQ64)
			|| !packed.ReadBits(CharacterReplicationConfig::YAW_BITS, yawQ64)
			|| !packed.ReadBits(CharacterReplicationConfig::PITCH_BITS, pitchQ64)
			|| !packed.ReadBits(CharacterReplicationConfig::VY_BITS, vyQ64)
			|| !packed.ReadBits(CharacterReplicationConfig::SPEED_BITS, spdQ64)
			|| !packed.ReadBits(CharacterReplicationConfig::MOVE_DIR_BITS, mxQ64)
			|| !packed.ReadBits(CharacterReplicationConfig::MOVE_DIR_BITS, myQ64)
			|| !packed.ReadBits(CharacterReplicationConfig::FLAGS_BITS_FULL160, flgQ64))
		{
			return false;
		}

		const uint32 pxQ    = static_cast<uint32>(pxQ64);
		const uint32 pyQ    = static_cast<uint32>(pyQ64);
		const uint32 pzQ    = static_cast<uint32>(pzQ64);
		const uint32 bodyYawQ = static_cast<uint32>(bodyYawQ64);
		const uint32 yawQ   = static_cast<uint32>(yawQ64);
		const uint32 pitchQ = static_cast<uint32>(pitchQ64);
		const uint32 vyQ    = static_cast<uint32>(vyQ64);
		const uint32 spdQ   = static_cast<uint32>(spdQ64);
		const uint32 mxQ    = static_cast<uint32>(mxQ64);
		const uint32 myQ    = static_cast<uint32>(myQ64);
		const uint32 flgQ   = static_cast<uint32>(flgQ64);

		state.pos.x           = detail::DecodeWorldPosX20(pxQ);
		state.pos.y           = detail::DecodeWorldPosY20(pyQ);
		state.pos.z           = detail::DecodeWorldPosZ20(pzQ);
		state.bodyYaw       = detail::DecodeAngleYaw16Fast(bodyYawQ);
		state.viewYaw       = detail::DecodeAngleYaw16Fast(yawQ);
		state.viewPitch     = detail::DecodeAnglePitch16Fast(pitchQ);
		state.verticalSpeed   = detail::DecodeVY12Fast(vyQ);
		state.horizontalSpeed = detail::DecodeSpeed10Fast(spdQ);
		state.moveDir.x       = detail::DecodeMoveDir8Fast(mxQ);
		state.moveDir.y       = detail::DecodeMoveDir8Fast(myQ);
		state.stateFlags      = flgQ;

		JAM_ASSERT_MSG(state.IsFinite(), "UnpackCharacterFull160() produced non-finite state");

		return (packed.Cursor() == 160);
	}



	/**
	 * @note Character Delta 128 (2 x u64 = 128 container)
	 *
	 *  -Layout (bit stream, LSB-first):
	 *      - delta pos               (36 = 12 + 12 + 12)
	 *      - delta bodyYaw           (12)
	 *      - delta viewYaw           (12)
	 *      - delta viewPitch         (12)
	 *      - verticalSpeed           (12)
	 *      - horizontalSpeed         (10)
	 *      - moveDir                 (16 = 8 + 8)
	 *      - stateFlags              (18)
	 */


	using PackedCharDelta128 = BitBuffer<uint64, 2>;

	inline bool PackCharacterDelta128(const px::Vec3& baselinePos, float baselineBodyYaw, float baselineViewYaw, float baselinePitch, const px::CharacterState& state, OUT PackedCharDelta128& packed)
	{
		JAM_ASSERT_MSG(baselinePos.IsFinite(),       "PackCharacterDelta128() expects finite baseline position");
		JAM_ASSERT_MSG(std::isfinite(baselineBodyYaw), "PackCharacterDelta128() expects finite baseline body yaw");
		JAM_ASSERT_MSG(std::isfinite(baselineViewYaw), "PackCharacterDelta128() expects finite baseline view yaw");
		JAM_ASSERT_MSG(std::isfinite(baselinePitch), "PackCharacterDelta128() expects finite baseline pitch");
		JAM_ASSERT_MSG(state.IsFinite(),             "PackCharacterDelta128() expects validated finite authoritative state");

		packed.Clear();

		const px::Vec3 dp = state.pos - baselinePos;

		if (fabsf(dp.x) > CharacterReplicationConfig::DELTA_POS_RANGE ||
			fabsf(dp.y) > CharacterReplicationConfig::DELTA_POS_RANGE ||
			fabsf(dp.z) > CharacterReplicationConfig::DELTA_POS_RANGE)
		{
			return false;
		}

		const float dbodyYaw = WrapPi(state.bodyYaw - baselineBodyYaw);
		const float dyaw     = WrapPi(state.viewYaw - baselineViewYaw);
		const float dpitch   = ClampFloat(state.viewPitch - baselinePitch, -px::PI_DIV_TWO, px::PI_DIV_TWO);

		const uint64 dxQ     = static_cast<uint64>(detail::EncodeDeltaPos12(dp.x));
		const uint64 dyQ     = static_cast<uint64>(detail::EncodeDeltaPos12(dp.y));
		const uint64 dzQ     = static_cast<uint64>(detail::EncodeDeltaPos12(dp.z));
		const uint64 dbodyYawQ = static_cast<uint64>(detail::EncodeDeltaYaw12Fast(dbodyYaw));
		const uint64 dyawQ   = static_cast<uint64>(detail::EncodeDeltaYaw12Fast(dyaw));
		const uint64 dpitchQ = static_cast<uint64>(detail::EncodeDeltaPitch12Fast(dpitch));
		const uint64 vyQ     = static_cast<uint64>(detail::EncodeVY12Fast(state.verticalSpeed));
		const uint64 spdQ    = static_cast<uint64>(detail::EncodeSpeed10Fast(state.horizontalSpeed));
		const uint64 mxQ     = static_cast<uint64>(detail::EncodeMoveDir8Fast(state.moveDir.x));
		const uint64 myQ     = static_cast<uint64>(detail::EncodeMoveDir8Fast(state.moveDir.y));
		const uint64 flgQ    = static_cast<uint64>(state.stateFlags) & ((1ull << CharacterReplicationConfig::FLAGS_BITS_DELTA128) - 1ull);

		if (!packed.WriteBits(dxQ, CharacterReplicationConfig::DELTA_POS_BITS)
			|| !packed.WriteBits(dyQ, CharacterReplicationConfig::DELTA_POS_BITS)
			|| !packed.WriteBits(dzQ, CharacterReplicationConfig::DELTA_POS_BITS)
			|| !packed.WriteBits(dbodyYawQ, CharacterReplicationConfig::DELTA_YAW_BITS)
			|| !packed.WriteBits(dyawQ, CharacterReplicationConfig::DELTA_YAW_BITS)
			|| !packed.WriteBits(dpitchQ, CharacterReplicationConfig::DELTA_PITCH_BITS)
			|| !packed.WriteBits(vyQ, CharacterReplicationConfig::VY_BITS)
			|| !packed.WriteBits(spdQ, CharacterReplicationConfig::SPEED_BITS)
			|| !packed.WriteBits(mxQ, CharacterReplicationConfig::MOVE_DIR_BITS)
			|| !packed.WriteBits(myQ, CharacterReplicationConfig::MOVE_DIR_BITS)
			|| !packed.WriteBits(flgQ, CharacterReplicationConfig::FLAGS_BITS_DELTA128))
		{
			return false;
		}

		return (packed.Cursor() == 128);
	}

	inline bool UnpackCharacterDelta128(const px::Vec3& baselinePos, float baselineBodyYaw, float baselineViewYaw, float baselinePitch, uint64 packed0, uint64 packed1, OUT px::CharacterState& state)
	{
		JAM_ASSERT_MSG(baselinePos.IsFinite(),       "UnpackCharacterDelta128() expects finite baseline position");
		JAM_ASSERT_MSG(std::isfinite(baselineBodyYaw), "UnpackCharacterDelta128() expects finite baseline body yaw");
		JAM_ASSERT_MSG(std::isfinite(baselineViewYaw), "UnpackCharacterDelta128() expects finite baseline view yaw");
		JAM_ASSERT_MSG(std::isfinite(baselinePitch), "UnpackCharacterDelta128() expects finite baseline yaw");

		BitBuffer<uint64, 2> buffer(std::array<uint64, 2>{ packed0, packed1 });
		uint64 dxQ64	 = 0;
		uint64 dyQ64	 = 0;
		uint64 dzQ64	 = 0;
		uint64 dbodyYawQ64 = 0;
		uint64 dyawQ64	 = 0;
		uint64 dpitchQ64 = 0;
		uint64 vyQ64	 = 0;
		uint64 spdQ64	 = 0;
		uint64 mxQ64	 = 0;
		uint64 myQ64	 = 0;
		uint64 flgQ64	 = 0;
		if (!buffer.ReadBits(CharacterReplicationConfig::DELTA_POS_BITS, dxQ64)
			|| !buffer.ReadBits(CharacterReplicationConfig::DELTA_POS_BITS, dyQ64)
			|| !buffer.ReadBits(CharacterReplicationConfig::DELTA_POS_BITS, dzQ64)
			|| !buffer.ReadBits(CharacterReplicationConfig::DELTA_YAW_BITS, dbodyYawQ64)
			|| !buffer.ReadBits(CharacterReplicationConfig::DELTA_YAW_BITS, dyawQ64)
			|| !buffer.ReadBits(CharacterReplicationConfig::DELTA_PITCH_BITS, dpitchQ64)
			|| !buffer.ReadBits(CharacterReplicationConfig::VY_BITS, vyQ64)
			|| !buffer.ReadBits(CharacterReplicationConfig::SPEED_BITS, spdQ64)
			|| !buffer.ReadBits(CharacterReplicationConfig::MOVE_DIR_BITS, mxQ64)
			|| !buffer.ReadBits(CharacterReplicationConfig::MOVE_DIR_BITS, myQ64)
			|| !buffer.ReadBits(CharacterReplicationConfig::FLAGS_BITS_DELTA128, flgQ64))
		{
			return false;
		}

		const uint32 dxQ     = static_cast<uint32>(dxQ64);
		const uint32 dyQ     = static_cast<uint32>(dyQ64);
		const uint32 dzQ     = static_cast<uint32>(dzQ64);
		const uint32 dbodyYawQ = static_cast<uint32>(dbodyYawQ64);
		const uint32 dyawQ   = static_cast<uint32>(dyawQ64);
		const uint32 dpitchQ = static_cast<uint32>(dpitchQ64);
		const uint32 vyQ     = static_cast<uint32>(vyQ64);
		const uint32 spdQ    = static_cast<uint32>(spdQ64);
		const uint32 mxQ     = static_cast<uint32>(mxQ64);
		const uint32 myQ     = static_cast<uint32>(myQ64);
		const uint32 flgQ    = static_cast<uint32>(flgQ64);

		state.pos = baselinePos + px::Vec3(
			detail::DecodeDeltaPos12(dxQ),
			detail::DecodeDeltaPos12(dyQ),
			detail::DecodeDeltaPos12(dzQ));

		state.bodyYaw		  = WrapPi(baselineBodyYaw + detail::DecodeDeltaYaw12Fast(dbodyYawQ));
		state.viewYaw		  = WrapPi(baselineViewYaw + detail::DecodeDeltaYaw12Fast(dyawQ));
		state.viewPitch		  = WrapPiHalf(baselinePitch + detail::DecodeDeltaPitch12Fast(dpitchQ));
		state.verticalSpeed   = detail::DecodeVY12Fast(vyQ);
		state.horizontalSpeed = detail::DecodeSpeed10Fast(spdQ);
		state.moveDir.x       = detail::DecodeMoveDir8Fast(mxQ);
		state.moveDir.y       = detail::DecodeMoveDir8Fast(myQ);
		state.stateFlags      = flgQ;

		JAM_ASSERT_MSG(state.IsFinite(), "UnpackCharacterDelta128() produced non-finite state");

		return (buffer.Cursor() == 128);
	}

}
