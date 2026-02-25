#pragma once


#include "PhysicsTypes.h"


namespace jam::net
{

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

        static constexpr float AOI_MAX = 100.f;

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

        static constexpr float DELTA_POS_RANGE = 2.f;
        static constexpr int32 DELTA_POS_BITS = 12;
    };

    struct CharacterReplicationConfig
    {
	    // Full (128 bits)

        static constexpr int32 POS_BITS = ReplicationConfig::POS_BITS;
        static constexpr int32 YAW_BITS = 16;
        static constexpr int32 PITCH_BITS = 16;
        static constexpr int32 VY_BITS = 12;
        static constexpr float VY_ABS_MAX = 64.f;

        static constexpr int32 SPEED_BITS = 10;
        static constexpr float SPEED_MAX = ReplicationConfig::MAX_LIN_SPEED;

        static constexpr int32 FLAGS_BITS_FULL = 14;

        static constexpr int32 MOVE_DIR_BITS = 8;
        static constexpr int32 FLAGS_BITS_FULL160 = 30;
        static constexpr int32 FLAGS_BITS_DELTA128 = 30;

        // Delta (96 bits)

        static constexpr float DELTA_POS_RANGE = ReplicationConfig::DELTA_POS_RANGE;
        static constexpr int32 DELTA_POS_BITS = ReplicationConfig::DELTA_POS_BITS;

        static constexpr int32 DELTA_YAW_BITS = 12;
        static constexpr int32 DELTA_PITCH_BITS = 12;

        static constexpr float DELTA_YAW_ABS_MAX = px::PI;
        static constexpr float DELTA_PITCH_ABS_MAX = px::PI_DIV_TWO;

        static constexpr int32 FLAGS_BITS_DELTA = 14;
    };

    inline uint64 ExtractBits64(uint64 v, int32 bitOffset, int32 bitCount)
    {
        if (bitCount <= 0) return 0;
        if (bitCount >= 64) return v >> bitOffset;
        const uint64 mask = (1ull << bitCount) - 1ull;
        return (v >> bitOffset) & mask;
    }

    inline void WriteBits64(OUT uint64& dst, int32 bitOffset, int32 bitCount, uint64 value)
    {
        if (bitCount <= 0) return;
        const uint64 mask = (bitCount >= 64) ? ~0ull : ((1ull << bitCount) - 1ull);
        dst |= (value & mask) << bitOffset;
    }

    inline void WriteBits96(OUT uint32& dst0, OUT uint32& dst1, OUT uint32& dst2, OUT  int32& bitCursor, uint32 value, int32 bitCount)
    {
        int32 remaining = bitCount;
        uint32 v = value;

        while (remaining > 0)
        {
            const int32 wordIndex = bitCursor / 32;
            const int32 inWord = bitCursor % 32;
            const int32 canWrite = std::min<int32>(remaining, 32 - inWord);

            const uint32 mask = (canWrite == 32) ? 0xFFFF'FFFFu : ((1u << canWrite) - 1u);
            const uint32 chunk = v & mask;

            if (wordIndex == 0) dst0 |= (chunk << inWord);
            else if (wordIndex == 1) dst1 |= (chunk << inWord);
            else dst2 |= (chunk << inWord);

            v >>= canWrite;
            remaining -= canWrite;
            bitCursor += canWrite;
        }
    }

    inline void WriteBits128(OUT uint64& dst0, OUT uint64& dst1, OUT int32& bitCursor, uint64 value, int32 bitCount)
    {
        int32 remaining = bitCount;
        uint64 v = value;

        while (remaining > 0)
        {
            const int32 wordIndex = bitCursor / 64;
            const int32 inWord = bitCursor % 64;
            const int32 canWrite = (remaining < (64 - inWord)) ? remaining : (64 - inWord);

            uint64 chunk = (canWrite == 64) ? v : (v & ((1ull << canWrite) - 1ull));

            if (wordIndex == 0) WriteBits64(dst0, inWord, canWrite, chunk);
            else                WriteBits64(dst1, inWord, canWrite, chunk);

            v >>= canWrite;
            remaining -= canWrite;
            bitCursor += canWrite;
        }
    }

    inline void WriteBits160(OUT uint64& dst0, OUT uint64& dst1, OUT uint32& dst2, OUT int32& bitCursor, uint64 value, int32 bitCount)
    {
        int32 remaining = bitCount;
        uint64 v = value;

        while (remaining > 0)
        {
            const int32 wordIndex = bitCursor / 64;
            const int32 inWord = bitCursor % 64;

            const int32 wordBits = (wordIndex < 2) ? 64 : 32;
            const int32 canWrite = std::min<int32>(remaining, wordBits - inWord);

            const uint64 chunkMask = (canWrite >= 64) ? ~0ull : ((1ull << canWrite) - 1ull);
            const uint64 chunk = v & chunkMask;

            if (wordIndex == 0)
            {
                WriteBits64(dst0, inWord, canWrite, chunk);
            }
            else if (wordIndex == 1)
            {
                WriteBits64(dst1, inWord, canWrite, chunk);
            }
            else
            {
                // dst2 is 32-bit container (bit [0..31])
                const uint32 mask32 = (canWrite == 32) ? 0xFFFF'FFFFu : ((1u << canWrite) - 1u);
                dst2 |= (static_cast<uint32>(chunk) & mask32) << inWord;
            }

            v >>= canWrite;
            remaining -= canWrite;
            bitCursor += canWrite;
        }
    }

    inline void WriteBits192(OUT uint64& dst0, OUT uint64& dst1, OUT uint64& dst2, OUT int32& bitCursor, uint64 value, int32 bitCount)
    {
        int32 remaining = bitCount;
        uint64 v = value;

        while (remaining > 0)
        {
            const int32 wordIndex = bitCursor / 64;
            const int32 inWord = bitCursor % 64;
            const int canWrite = (remaining < (64 - inWord)) ? remaining : (64 - inWord);

            uint64 chunk = (canWrite == 64) ? v : (v & ((1ull << canWrite) - 1ull));

            if (wordIndex == 0)         WriteBits64(dst0, inWord, canWrite, chunk);
            else if (wordIndex == 1)    WriteBits64(dst1, inWord, canWrite, chunk);
            else                        WriteBits64(dst2, inWord, canWrite, chunk);

            v >>= canWrite;
            remaining -= canWrite;
            bitCursor += canWrite;
        }
    }


    inline uint32 ReadBits96(uint32 src0, uint32 src1, uint32 src2, OUT int32& bitCursor, int32 bitCount)
    {
        int32 remaining = bitCount;
        uint32 out = 0;
        int32 outShift = 0;

        while (remaining > 0)
        {
            const int32 wordIndex = bitCursor / 32;
            const int32 inWord = bitCursor % 32;
            const int32 canRead = std::min<int32>(remaining, 32 - inWord);

            uint32 chunk = 0;
            if (wordIndex == 0) chunk |= (src0 >> inWord);
            else if (wordIndex == 1) chunk |= (src1 >> inWord);
            else chunk |= (src2 >> inWord);

            const uint32 mask = (canRead == 32) ? 0xFFFF'FFFFu : ((1u << canRead) - 1u);
            chunk &= mask;

            out |= (chunk << outShift);

            remaining -= canRead;
            bitCursor += canRead;
            outShift += canRead;
        }

        return out;
    }

    inline uint64 ReadBits128(uint64 src0, uint64 src1, OUT int32& bitCursor, int32 bitCount)
    {
        int32  remaining = bitCount;
        uint64 out = 0;
        int32  outShift = 0;

        while (remaining > 0)
        {
            const int32 wordIndex = bitCursor / 64;
            const int32 inWord    = bitCursor % 64;
            const int32 canRead   = (remaining < (64 - inWord)) ? remaining : (64 - inWord);

            uint64 chunk = 0;
            if (wordIndex == 0) chunk = ExtractBits64(src0, inWord, canRead);
            else                chunk = ExtractBits64(src1, inWord, canRead);

            out |= (chunk << outShift);

            remaining -= canRead;
            bitCursor += canRead;
            outShift  += canRead;
        }

        return out;
    }

    inline uint64 ReadBits160(uint64 src0, uint64 src1, uint32 src2, OUT int32& bitCursor, int32 bitCount)
    {
        int32 remaining = bitCount;
        uint64 out = 0;
        int32 outShift = 0;

        while (remaining > 0)
        {
            const int32 wordIndex = bitCursor / 64;
            const int32 inWord = bitCursor % 64;

            const int32 wordBits = (wordIndex < 2) ? 64 : 32;
            const int32 canRead = std::min<int32>(remaining, wordBits - inWord);

            uint64 chunk = 0;
            if (wordIndex == 0)
            {
                chunk = ExtractBits64(src0, inWord, canRead);
            }
            else if (wordIndex == 1)
            {
                chunk = ExtractBits64(src1, inWord, canRead);
            }
            else
            {
                const uint32 mask32 = (canRead == 32) ? 0xFFFF'FFFFu : ((1u << canRead) - 1u);
                chunk = static_cast<uint64>((src2 >> inWord) & mask32);
            }

            out |= (chunk << outShift);

            remaining -= canRead;
            bitCursor += canRead;
            outShift += canRead;
        }

        return out;
    }

    inline uint64 ReadBits192(uint64 src0, uint64 src1, uint64 src2, OUT int32& bitCursor, int32 bitCount)
    {
        int remaining = bitCount;
        uint64 out = 0;
        int outShift = 0;

        while (remaining > 0)
        {
            const int32 wordIndex = bitCursor / 64;
            const int32 inWord    = bitCursor % 64;
            const int32 canRead   = (remaining < (64 - inWord)) ? remaining : (64 - inWord);

            uint64 chunk = 0;
            if (wordIndex == 0)         chunk = ExtractBits64(src0, inWord, canRead);
            else if (wordIndex == 1)    chunk = ExtractBits64(src1, inWord, canRead);
            else                        chunk = ExtractBits64(src2, inWord, canRead);

            out |= (chunk << outShift);

            remaining -= canRead;
            bitCursor += canRead;
            outShift += canRead;
        }

        return out;
    }

    inline float ClampFloat(float v, float lo, float hi)
    {
        return (v < lo) ? lo : (v > hi ? hi : v);
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
    inline uint32 EncodeSNorm(float v, float absMax, int bits)
    {
        if (absMax <= 0.f) return 0;
        v = ClampFloat(v, -absMax, absMax);
        const float t = (v / absMax) * 0.5f + 0.5f;
        const float q = t * static_cast<float>((1u << bits) - 1u) + 0.5f;
        return static_cast<uint32>(q);
    }

    inline float DecodeSNorm(uint32 q, float absMax, int bits)
    {
        if (absMax <= 0.f) return 0.f;
        const float t = static_cast<float>(q) / static_cast<float>((1u << bits) - 1u);
        const float sn = t * 2.f - 1.f;
        return sn * absMax;
    }

    inline uint32 EncodeSNorm8(float v)
    {
        return EncodeSNorm(v, 1.0f, 8);
    }

    inline float DecodeSNorm8(uint32 q)
    {
        return DecodeSNorm(q, 1.0f, 8);
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

    inline uint32 EncodeAngleYaw16(float yaw)
    {
        yaw = WrapPi(yaw);
        return EncodeUNorm(yaw, -px::PI, px::PI, CharacterReplicationConfig::YAW_BITS);
    }

    inline float DecodeAngleYaw16(uint32 q)
    {
        return DecodeUNorm(q, -px::PI, px::PI, CharacterReplicationConfig::YAW_BITS);
    }

    inline uint32 EncodeAnglePitch16(float pitch)
    {
        pitch = WrapPiHalf(pitch);
        return EncodeUNorm(pitch, -px::PI_DIV_TWO, px::PI_DIV_TWO, CharacterReplicationConfig::PITCH_BITS);
    }

    inline float DecodeAnglePitch16(uint32 q)
    {
        return EncodeUNorm(q, -px::PI_DIV_TWO, px::PI_DIV_TWO, CharacterReplicationConfig::PITCH_BITS);
    }

    inline uint32 EncodeSpeed10(float speed)
    {
        return EncodeUNorm(speed, 0.f, CharacterReplicationConfig::SPEED_MAX, CharacterReplicationConfig::SPEED_BITS);
    }

    inline float DecodeSpeed10(uint32 q)
    {
        return DecodeUNorm(q, 0.f, CharacterReplicationConfig::SPEED_MAX, CharacterReplicationConfig::SPEED_BITS);
    }

    inline uint32 EncodeVY(float vy)
    {
        return EncodeSNorm(vy, CharacterReplicationConfig::VY_ABS_MAX, CharacterReplicationConfig::VY_BITS);
    }

    inline float DecodeVY12(uint32 q)
    {
        return DecodeSNorm(q, CharacterReplicationConfig::VY_ABS_MAX, CharacterReplicationConfig::VY_BITS);
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

    inline PackedQuat48 PackQuat48(const px::Quat& in)
    {
        px::Quat q = in;
        q.Normalize();

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

        static constexpr float kRange = 0.7071067811865475f; // 1/sqrt(2)

        const uint64 qa = EncodeSNorm(a0, kRange, 15);
        const uint64 qb = EncodeSNorm(a1, kRange, 15);
        const uint64 qc = EncodeSNorm(a2, kRange, 15);

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
        static constexpr float kRange = 0.7071067811865475f; // 1/sqrt(2)

        const int largest = static_cast<int>((packed.data >> 0) & 0x3ull);
        const bool sign = (((packed.data >> 2) & 0x1ull) != 0);

        const uint32 qa = static_cast<uint32>((packed.data >> 3) & 0x7FFFull);
        const uint32 qb = static_cast<uint32>((packed.data >> 18) & 0x7FFFull);
        const uint32 qc = static_cast<uint32>((packed.data >> 33) & 0x7FFFull);

        const float a0 = DecodeSNorm(qa, kRange, 15);
        const float a1 = DecodeSNorm(qb, kRange, 15);
        const float a2 = DecodeSNorm(qc, kRange, 15);

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
    struct PackedRigidFull192
    {
        uint64 data0 = 0;
        uint64 data1 = 0;
        uint64 data2 = 0;

        bool operator==(const PackedRigidFull192&) const = default;
    };


    inline bool PackRigidFull192(const px::RigidState& rs, OUT PackedRigidFull192& packed)
    {
        packed.data0 = 0;
        packed.data1 = 0;
        packed.data2 = 0;

        if (!rs.IsFinite())
            return false;

        const uint64        px = EncodeUNorm(rs.pose.p.x, ReplicationConfig::MIN_WORLD_X, ReplicationConfig::MAX_WORLD_X, ReplicationConfig::POS_BITS);
        const uint64        py = EncodeUNorm(rs.pose.p.y, ReplicationConfig::MIN_WORLD_Y, ReplicationConfig::MAX_WORLD_Y, ReplicationConfig::POS_BITS);
        const uint64        pz = EncodeUNorm(rs.pose.p.z, ReplicationConfig::MIN_WORLD_Z, ReplicationConfig::MAX_WORLD_Z, ReplicationConfig::POS_BITS);
        const PackedQuat48  q  = PackQuat48(rs.pose.q);
        const PackedVel42   lv = PackVel42(rs.linVel, ReplicationConfig::MAX_LIN_SPEED);
        const PackedVel42   av = PackVel42(rs.angVel, ReplicationConfig::MAX_ANG_SPEED);

        int32 cursor = 0;

        WriteBits192(packed.data0, packed.data1, packed.data2, cursor, px, ReplicationConfig::POS_BITS);
        WriteBits192(packed.data0, packed.data1, packed.data2, cursor, py, ReplicationConfig::POS_BITS);
        WriteBits192(packed.data0, packed.data1, packed.data2, cursor, pz, ReplicationConfig::POS_BITS);
        WriteBits192(packed.data0, packed.data1, packed.data2, cursor, q.data, 48);
        WriteBits192(packed.data0, packed.data1, packed.data2, cursor, lv.data, 42);
        WriteBits192(packed.data0, packed.data1, packed.data2, cursor, av.data, 42);

        return (cursor == 192);
    }

    inline bool UnpackRigidFull192(uint64 packed0, uint64 packed1, uint64 packed2, OUT px::RigidState& state)
    {
        int32 cursor = 0;

        const uint32 pxQ = static_cast<uint32>(ReadBits192(packed0, packed1, packed2, cursor, ReplicationConfig::POS_BITS));
        const uint32 pyQ = static_cast<uint32>(ReadBits192(packed0, packed1, packed2, cursor, ReplicationConfig::POS_BITS));
        const uint32 pzQ = static_cast<uint32>(ReadBits192(packed0, packed1, packed2, cursor, ReplicationConfig::POS_BITS));

        PackedQuat48 q{};
        q.data = ReadBits192(packed0, packed1, packed2, cursor, 48);

        PackedVel42 lv{};
        lv.data = ReadBits192(packed0, packed1, packed2, cursor, 42);

        PackedVel42 av{};
        av.data = ReadBits192(packed0, packed1, packed2, cursor, 42);

        state.pose.p.x = DecodeUNorm(pxQ, ReplicationConfig::MIN_WORLD_X, ReplicationConfig::MAX_WORLD_X, ReplicationConfig::POS_BITS);
        state.pose.p.y = DecodeUNorm(pyQ, ReplicationConfig::MIN_WORLD_Y, ReplicationConfig::MAX_WORLD_Y, ReplicationConfig::POS_BITS);
        state.pose.p.z = DecodeUNorm(pzQ, ReplicationConfig::MIN_WORLD_Z, ReplicationConfig::MAX_WORLD_Z, ReplicationConfig::POS_BITS);
        state.pose.q   = UnpackQuat48(q);
        state.linVel   = UnpackVel42(lv, ReplicationConfig::MAX_LIN_SPEED);
        state.angVel   = UnpackVel42(av, ReplicationConfig::MAX_ANG_SPEED);

        return (cursor == 192);
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

    struct PackedRigidDelta128
    {
        uint64  data0 = 0;
        uint64  data1 = 0;

        bool operator==(const PackedRigidDelta128& other) const = default;
    };

    inline bool PackRigidDelta128(const px::Vec3& baselinePos, const px::Quat& baselineRot, const px::RigidState& state, OUT PackedRigidDelta128& packed)
    {
        packed.data0 = 0;
        packed.data1 = 0;

        if (!baselinePos.IsFinite() || !baselineRot.IsFinite() || !state.IsFinite())
            return false;

        const px::Vec3 dp = state.pose.p - baselinePos;

        if (fabsf(dp.x) > ReplicationConfig::DELTA_POS_RANGE ||
            fabsf(dp.y) > ReplicationConfig::DELTA_POS_RANGE ||
            fabsf(dp.z) > ReplicationConfig::DELTA_POS_RANGE)
        {
            return false;
        }

        px::Quat br = baselineRot;
        br.Normalize();

        px::Quat r = state.pose.q;
        r.Normalize();

        // relative rotation: dr = inv(baseline) * current
        px::Quat invBr = br.Conjugate();
        px::Quat dr    = invBr * r;
        dr.Normalize();

        const uint64        dx  = EncodeSNorm(dp.x, ReplicationConfig::DELTA_POS_RANGE, ReplicationConfig::DELTA_POS_BITS);
        const uint64        dy  = EncodeSNorm(dp.y, ReplicationConfig::DELTA_POS_RANGE, ReplicationConfig::DELTA_POS_BITS);
        const uint64        dz  = EncodeSNorm(dp.z, ReplicationConfig::DELTA_POS_RANGE, ReplicationConfig::DELTA_POS_BITS);
        const PackedQuat48  q   = PackQuat48(dr);
        const PackedVel42   lv  = PackVel42(state.linVel, ReplicationConfig::MAX_LIN_SPEED);

        constexpr uint64    blank = 0;

        int32 cursor = 0;

        WriteBits128(packed.data0, packed.data1, cursor, dx, ReplicationConfig::DELTA_POS_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, dy, ReplicationConfig::DELTA_POS_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, dz, ReplicationConfig::DELTA_POS_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, q.data, 48);
        WriteBits128(packed.data0, packed.data1, cursor, lv.data, 42);
        WriteBits128(packed.data0, packed.data1, cursor, blank, 2);

        return (cursor == 128);
    }


    inline bool UnpackRigidDelta128(const px::Vec3& baselinePos, const px::Quat baselineRot, uint64 data0, uint64 data1, OUT px::RigidState& state)
    {
        if (!baselinePos.IsFinite() || !baselineRot.IsFinite())
        {
            return false;
        }

        px::Quat br = baselineRot;
        br.Normalize();

        int32 cursor = 0;

        const uint32 dxQ = static_cast<uint32>(ReadBits128(data0, data1, cursor, ReplicationConfig::DELTA_POS_BITS));
        const uint32 dyQ = static_cast<uint32>(ReadBits128(data0, data1, cursor, ReplicationConfig::DELTA_POS_BITS));
        const uint32 dzQ = static_cast<uint32>(ReadBits128(data0, data1, cursor, ReplicationConfig::DELTA_POS_BITS));

        PackedQuat48 q{};
        q.data = ReadBits128(data0, data1, cursor, 48);

        PackedVel42 lv{};
        lv.data = ReadBits128(data0, data1, cursor, 42);

        (void)ReadBits128(data0, data1, cursor, 2); // blank 2-bit

        const float dx = DecodeSNorm(dxQ, ReplicationConfig::DELTA_POS_RANGE, ReplicationConfig::DELTA_POS_BITS);
        const float dy = DecodeSNorm(dyQ, ReplicationConfig::DELTA_POS_RANGE, ReplicationConfig::DELTA_POS_BITS);
        const float dz = DecodeSNorm(dzQ, ReplicationConfig::DELTA_POS_RANGE, ReplicationConfig::DELTA_POS_BITS);

        const px::Vec3 dp(dx, dy, dz);

        const px::Quat dr = UnpackQuat48(q);
        px::Quat r = br * dr;
        r.Normalize();

        state.pose.p = baselinePos + dp;
        state.pose.q = r;
        state.linVel = UnpackVel42(lv, ReplicationConfig::MAX_LIN_SPEED);

        return (cursor == 128);
    }



    /**
     * @note Character Full 160 (2 x u64 + u32 = 160 container)
     *
     *  -Layout (bit stream, LSB-first):
     *      - pos               (60 = 20 + 20 + 20)
     *      - facingYaw         (16)
     *      - facingPitch       (16)
     *      - verticalSpeed     (12)
     *      - horizontalSpeed   (10)
     *      - moveDir           (16 = 8 + 8)
     *      - stateFlags        (30)
     */


    struct PackedCharacterFull160
    {
        uint64 data0 = 0;
        uint64 data1 = 0;
        uint32 data2 = 0;

        bool operator==(const PackedCharacterFull160&) const = default;
    };


    inline bool PackCharacterFull160(const px::CharacterState& state, OUT PackedCharacterFull160& packed)
    {
        packed.data0 = packed.data1 = 0;
        packed.data2 = 0;

        if (!state.IsFinite()) return false;

        const uint64 px     = static_cast<uint64>(EncodeUNorm(state.pos.x, ReplicationConfig::MIN_WORLD_X, ReplicationConfig::MAX_WORLD_X, CharacterReplicationConfig::POS_BITS));
        const uint64 py     = static_cast<uint64>(EncodeUNorm(state.pos.y, ReplicationConfig::MIN_WORLD_Y, ReplicationConfig::MAX_WORLD_Y, CharacterReplicationConfig::POS_BITS));
        const uint64 pz     = static_cast<uint64>(EncodeUNorm(state.pos.z, ReplicationConfig::MIN_WORLD_Z, ReplicationConfig::MAX_WORLD_Z, CharacterReplicationConfig::POS_BITS));
        const uint64 yawQ   = static_cast<uint64>(EncodeAngleYaw16(state.facingYaw));
        const uint64 pitchQ = static_cast<uint64>(EncodeAnglePitch16(state.facingPitch));
        const uint64 vyQ    = static_cast<uint64>(EncodeVY(state.verticalSpeed));
        const uint64 spdQ   = static_cast<uint64>(EncodeSpeed10(state.horizontalSpeed));
        const uint64 mxQ    = static_cast<uint64>(EncodeSNorm8(state.moveDir.x));
        const uint64 myQ    = static_cast<uint64>(EncodeSNorm8(state.moveDir.y));
        const uint64 flgQ   = static_cast<uint64>(state.stateFlags) & ((1ull << CharacterReplicationConfig::FLAGS_BITS_FULL160) - 1ull);

        int32 cursor = 0;
        WriteBits160(packed.data0, packed.data1, packed.data2, cursor, px, CharacterReplicationConfig::POS_BITS);
        WriteBits160(packed.data0, packed.data1, packed.data2, cursor, py, CharacterReplicationConfig::POS_BITS);
        WriteBits160(packed.data0, packed.data1, packed.data2, cursor, pz, CharacterReplicationConfig::POS_BITS);
        WriteBits160(packed.data0, packed.data1, packed.data2, cursor, yawQ, CharacterReplicationConfig::YAW_BITS);
        WriteBits160(packed.data0, packed.data1, packed.data2, cursor, pitchQ, CharacterReplicationConfig::PITCH_BITS);
        WriteBits160(packed.data0, packed.data1, packed.data2, cursor, vyQ, CharacterReplicationConfig::VY_BITS);
        WriteBits160(packed.data0, packed.data1, packed.data2, cursor, spdQ, CharacterReplicationConfig::SPEED_BITS);
        WriteBits160(packed.data0, packed.data1, packed.data2, cursor, mxQ, CharacterReplicationConfig::MOVE_DIR_BITS);
        WriteBits160(packed.data0, packed.data1, packed.data2, cursor, myQ, CharacterReplicationConfig::MOVE_DIR_BITS);
        WriteBits160(packed.data0, packed.data1, packed.data2, cursor, flgQ, CharacterReplicationConfig::FLAGS_BITS_FULL160);

        return (cursor == 160);
    }

    inline bool UnpackCharacterFull160(uint64 packed0, uint64 packed1, uint64 packed2, OUT px::CharacterState& state)
    {
        int32 cursor = 0;

        const uint32 pxQ    = static_cast<uint32>(ReadBits160(packed0, packed1, packed2, cursor, CharacterReplicationConfig::POS_BITS));
        const uint32 pyQ    = static_cast<uint32>(ReadBits160(packed0, packed1, packed2, cursor, CharacterReplicationConfig::POS_BITS));
        const uint32 pzQ    = static_cast<uint32>(ReadBits160(packed0, packed1, packed2, cursor, CharacterReplicationConfig::POS_BITS));
        const uint32 yawQ   = static_cast<uint32>(ReadBits160(packed0, packed1, packed2, cursor, CharacterReplicationConfig::YAW_BITS));
        const uint32 pitchQ = static_cast<uint32>(ReadBits160(packed0, packed1, packed2, cursor, CharacterReplicationConfig::PITCH_BITS));
        const uint32 vyQ    = static_cast<uint32>(ReadBits160(packed0, packed1, packed2, cursor, CharacterReplicationConfig::VY_BITS));
        const uint32 spdQ   = static_cast<uint32>(ReadBits160(packed0, packed1, packed2, cursor, CharacterReplicationConfig::SPEED_BITS));
        const uint32 mxQ    = static_cast<uint32>(ReadBits160(packed0, packed1, packed2, cursor, CharacterReplicationConfig::MOVE_DIR_BITS));
        const uint32 myQ    = static_cast<uint32>(ReadBits160(packed0, packed1, packed2, cursor, CharacterReplicationConfig::MOVE_DIR_BITS));
        const uint32 flgQ   = static_cast<uint32>(ReadBits160(packed0, packed1, packed2, cursor, CharacterReplicationConfig::FLAGS_BITS_FULL160));

        state.pos.x           = DecodeUNorm(pxQ, ReplicationConfig::MIN_WORLD_X, ReplicationConfig::MAX_WORLD_X, CharacterReplicationConfig::POS_BITS);
        state.pos.y           = DecodeUNorm(pyQ, ReplicationConfig::MIN_WORLD_Y, ReplicationConfig::MAX_WORLD_Y, CharacterReplicationConfig::POS_BITS);
        state.pos.z           = DecodeUNorm(pzQ, ReplicationConfig::MIN_WORLD_Z, ReplicationConfig::MAX_WORLD_Z, CharacterReplicationConfig::POS_BITS);
        state.facingYaw       = DecodeAngleYaw16(yawQ);
        state.facingPitch     = DecodeAnglePitch16(pitchQ);
        state.verticalSpeed   = DecodeVY12(vyQ);
        state.horizontalSpeed = DecodeSpeed10(spdQ);
        state.moveDir.x       = DecodeSNorm8(mxQ);
        state.moveDir.y       = DecodeSNorm8(myQ);
        state.stateFlags      = flgQ;

        return (cursor == 160);
    }



    /**
	 * @note Character Delta 128 (2 x u64 = 128 container)
	 *
	 *  -Layout (bit stream, LSB-first):
	 *      - delta pos               (36 = 12 + 12 + 12)
	 *      - delta facingYaw         (12)
	 *      - delta facingPitch       (12)
	 *      - verticalSpeed           (12)
	 *      - horizontalSpeed         (10)
	 *      - moveDir                 (16 = 8 + 8)
	 *      - stateFlags              (30)
	 */


    struct PackedCharacterDelta128
    {
        uint64          data0 = 0;
        uint64          data1 = 0;

        bool operator==(const PackedCharacterDelta128&) const = default;
    };

    inline bool PackCharacterDelta128(const px::Vec3& baselinePos, float baselineFacingYaw, float baselineFacingPitch, const px::CharacterState& state, OUT PackedCharacterDelta128& packed)
    {
        packed.data0 = packed.data1 = 0;

        if (!baselinePos.IsFinite() || !std::isfinite(baselineFacingYaw) || !std::isfinite(baselineFacingPitch) || !state.IsFinite())
            return false;

        const px::Vec3 dp = state.pos - baselinePos;

        if (fabsf(dp.x) > CharacterReplicationConfig::DELTA_POS_RANGE ||
            fabsf(dp.y) > CharacterReplicationConfig::DELTA_POS_RANGE ||
            fabsf(dp.z) > CharacterReplicationConfig::DELTA_POS_RANGE)
        {
            return false;
        }

        const float dyaw     = WrapPi(state.facingYaw - baselineFacingYaw);
        const float dpitch   = ClampFloat(state.facingPitch - baselineFacingPitch, -px::PI_DIV_TWO, px::PI_DIV_TWO);

        const uint64 dxQ     = static_cast<uint64>(EncodeSNorm(dp.x, CharacterReplicationConfig::DELTA_POS_RANGE, CharacterReplicationConfig::DELTA_POS_BITS));
        const uint64 dyQ     = static_cast<uint64>(EncodeSNorm(dp.y, CharacterReplicationConfig::DELTA_POS_RANGE, CharacterReplicationConfig::DELTA_POS_BITS));
        const uint64 dzQ     = static_cast<uint64>(EncodeSNorm(dp.z, CharacterReplicationConfig::DELTA_POS_RANGE, CharacterReplicationConfig::DELTA_POS_BITS));
        const uint64 dyawQ   = static_cast<uint64>(EncodeSNorm(dyaw, CharacterReplicationConfig::DELTA_YAW_ABS_MAX, CharacterReplicationConfig::DELTA_YAW_BITS));
        const uint64 dpitchQ = static_cast<uint64>(EncodeSNorm(dpitch, CharacterReplicationConfig::DELTA_PITCH_ABS_MAX, CharacterReplicationConfig::DELTA_PITCH_BITS));
        const uint64 vyQ     = static_cast<uint64>(EncodeVY(state.verticalSpeed));
        const uint64 spdQ    = static_cast<uint64>(EncodeSpeed10(state.horizontalSpeed));
        const uint64 mxQ     = static_cast<uint64>(EncodeSNorm8(state.moveDir.x));
        const uint64 myQ     = static_cast<uint64>(EncodeSNorm8(state.moveDir.y));
        const uint64 flgQ    = static_cast<uint64>(state.stateFlags) & ((1ull << CharacterReplicationConfig::FLAGS_BITS_DELTA128) - 1ull);

        int32 cursor = 0;

        WriteBits128(packed.data0, packed.data1, cursor, dxQ, CharacterReplicationConfig::DELTA_POS_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, dyQ, CharacterReplicationConfig::DELTA_POS_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, dzQ, CharacterReplicationConfig::DELTA_POS_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, dyawQ, CharacterReplicationConfig::DELTA_YAW_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, dpitchQ, CharacterReplicationConfig::DELTA_PITCH_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, vyQ, CharacterReplicationConfig::VY_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, spdQ, CharacterReplicationConfig::SPEED_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, mxQ, CharacterReplicationConfig::MOVE_DIR_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, myQ, CharacterReplicationConfig::MOVE_DIR_BITS);
        WriteBits128(packed.data0, packed.data1, cursor, flgQ, CharacterReplicationConfig::FLAGS_BITS_DELTA128);

        return (cursor == 128);
    }

    inline bool UnpackCharacterDelta128(const px::Vec3& baselinePos, float baselineFacingYaw, float baselineFacingPitch, uint64 packed0, uint64 packed1, OUT px::CharacterState& state)
    {

        if (!baselinePos.IsFinite() || !std::isfinite(baselineFacingYaw) || !std::isfinite(baselineFacingPitch))
            return false;

        int32 cursor = 0;

        const uint32 dxQ     = static_cast<uint32>(ReadBits128(packed0, packed1, cursor, CharacterReplicationConfig::DELTA_POS_BITS));
        const uint32 dyQ     = static_cast<uint32>(ReadBits128(packed0, packed1, cursor, CharacterReplicationConfig::DELTA_POS_BITS));
        const uint32 dzQ     = static_cast<uint32>(ReadBits128(packed0, packed1, cursor, CharacterReplicationConfig::DELTA_POS_BITS));
        const uint32 dyawQ   = static_cast<uint32>(ReadBits128(packed0, packed1, cursor, CharacterReplicationConfig::DELTA_YAW_BITS));
        const uint32 dpitchQ = static_cast<uint32>(ReadBits128(packed0, packed1, cursor, CharacterReplicationConfig::DELTA_PITCH_BITS));
        const uint32 vyQ     = static_cast<uint32>(ReadBits128(packed0, packed1, cursor, CharacterReplicationConfig::VY_BITS));
        const uint32 spdQ    = static_cast<uint32>(ReadBits128(packed0, packed1, cursor, CharacterReplicationConfig::SPEED_BITS));
        const uint32 mxQ     = static_cast<uint32>(ReadBits128(packed0, packed1, cursor, CharacterReplicationConfig::MOVE_DIR_BITS));
        const uint32 myQ     = static_cast<uint32>(ReadBits128(packed0, packed1, cursor, CharacterReplicationConfig::MOVE_DIR_BITS));
        const uint32 flgQ    = static_cast<uint32>(ReadBits128(packed0, packed1, cursor, CharacterReplicationConfig::FLAGS_BITS_DELTA128));

        state.pos = baselinePos + px::Vec3(
            DecodeSNorm(dxQ, CharacterReplicationConfig::DELTA_POS_RANGE, CharacterReplicationConfig::DELTA_POS_BITS),
            DecodeSNorm(dyQ, CharacterReplicationConfig::DELTA_POS_RANGE, CharacterReplicationConfig::DELTA_POS_BITS),
            DecodeSNorm(dzQ, CharacterReplicationConfig::DELTA_POS_RANGE, CharacterReplicationConfig::DELTA_POS_BITS));

        state.facingYaw       = WrapPi(baselineFacingYaw + DecodeSNorm(dyawQ, CharacterReplicationConfig::DELTA_YAW_ABS_MAX, CharacterReplicationConfig::DELTA_YAW_BITS));
        state.facingPitch     = WrapPiHalf(baselineFacingPitch + DecodeSNorm(dpitchQ, CharacterReplicationConfig::DELTA_PITCH_ABS_MAX, CharacterReplicationConfig::DELTA_PITCH_BITS));
        state.verticalSpeed   = DecodeVY12(vyQ);
        state.horizontalSpeed = DecodeSpeed10(spdQ);
        state.moveDir.x       = DecodeSNorm8(mxQ);
        state.moveDir.y       = DecodeSNorm8(myQ);
        state.stateFlags      = flgQ;

    	return (cursor == 128);
    }

}
