#pragma once

#include <jambase/JamTypes.h>

#include <cstdint>
#include <cmath>
#include <optional>

namespace jam::px
{
	inline constexpr float PI = 3.14159265358979323846f;
	inline constexpr float TWO_PI = 6.28318530717958647692f;
	inline constexpr float PI_DIV_TWO = 1.57079632679489661923f;
	inline constexpr float PI_DIV_FOUR = 0.78539816339744830962f;

	inline constexpr float EPSILON = 1e-6f;


	inline float Clamp(float v, float lo, float hi) noexcept
	{
		return (v < lo) ? lo : (v > hi ? hi : v);
	}

	inline float Saturate(float v) noexcept
	{
		return Clamp(v, 0.0f, 1.0f);
	}


	struct Vec2
	{
		float x{}, y{};

		constexpr Vec2() = default;
		constexpr Vec2(float _x, float _y) : x(_x), y(_y) {}
		static constexpr Vec2 Zero() noexcept { return Vec2(0.f, 0.f); }

		constexpr Vec2 operator+() const noexcept { return *this; }
		constexpr Vec2 operator-() const noexcept { return Vec2(-x, -y); }

		constexpr Vec2 operator+(const Vec2& r) const noexcept { return { x + r.x, y + r.y }; }
		constexpr Vec2 operator-(const Vec2& r) const noexcept { return { x - r.x, y - r.y }; }
		constexpr Vec2 operator*(float s)		const noexcept { return { x * s, y * s }; }
		constexpr Vec2 operator/(float s)		const noexcept { return { x / s, y / s }; }

		constexpr Vec2& operator+=(const Vec2& r) noexcept { x += r.x; y += r.y; return *this; }
		constexpr Vec2& operator-=(const Vec2& r) noexcept { x -= r.x; y -= r.y; return *this; }
		constexpr Vec2& operator*=(float s)		  noexcept { x *= s; y *= s; return *this; }
		constexpr Vec2& operator/=(float s)		  noexcept { x /= s; y /= s; return *this; }

		constexpr bool operator==(const Vec2&) const noexcept = default;

		constexpr float Dot(const Vec2& r) const noexcept { return x * r.x + y * r.y; }

		float MagnitudeSquared() const noexcept { return x * x + y * y; }
		float Magnitude()		 const noexcept { return std::sqrt(MagnitudeSquared()); }

		float DistanceSquared(const Vec2& r) const noexcept { return (*this - r).MagnitudeSquared(); }
		float Distance(const Vec2& r)		 const noexcept { return std::sqrt(DistanceSquared(r)); }

		bool  IsFinite() const noexcept { return std::isfinite(x) && std::isfinite(y); }

		bool  IsZero(float eps = EPSILON) const noexcept { return std::fabs(x) <= eps && std::fabs(y) <= eps; }

		Vec2  GetNormalized(float eps = EPSILON) const noexcept
		{
			const float m2 = MagnitudeSquared();
			if (m2 <= eps * eps)
				return Vec2::Zero();

			const float inv = 1.0f / std::sqrt(m2);
			return (*this) * inv;
		}

		void Normalize(float eps = EPSILON) noexcept
		{
			*this = GetNormalized(eps);
		}

		Vec2 GetClamped(float maxLength) const noexcept
		{
			const float m = Magnitude();
			if (m <= maxLength || m <= EPSILON)
				return *this;
			return (*this) * (maxLength / m);
		}

		static Vec2 Lerp(const Vec2& a, const Vec2& b, float t) noexcept
		{
			t = Saturate(t);
			return a + (b - a) * t;
		}
	};
	inline constexpr Vec2 operator*(float s, const Vec2& v) noexcept { return v * s; }

	struct Vec3
	{
		float x{}, y{}, z{};

		constexpr Vec3() = default;
		constexpr Vec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
		static constexpr Vec3 Zero() noexcept { return Vec3(0.f, 0.f, 0.f); }

		constexpr Vec3 operator+() const noexcept { return *this; }
		constexpr Vec3 operator-() const noexcept { return Vec3(-x, -y, -z); }

		constexpr Vec3 operator+(const Vec3& r) const noexcept { return { x + r.x, y + r.y, z + r.z }; }
		constexpr Vec3 operator-(const Vec3& r) const noexcept { return { x - r.x, y - r.y, z - r.z }; }
		constexpr Vec3 operator*(float s)		const noexcept { return { x * s, y * s, z * s }; }
		constexpr Vec3 operator/(float s)		const noexcept { return { x / s, y / s, z / s }; }

		constexpr Vec3& operator+=(const Vec3& r) noexcept { x += r.x; y += r.y; z += r.z; return *this; }
		constexpr Vec3& operator-=(const Vec3& r) noexcept { x -= r.x; y -= r.y; z -= r.z; return *this; }
		constexpr Vec3& operator*=(float s)		  noexcept { x *= s; y *= s; z *= s; return *this; }
		constexpr Vec3& operator/=(float s)		  noexcept { x /= s; y /= s; z /= s; return *this; }

		constexpr bool	operator==(const Vec3&) const noexcept = default;

		constexpr float Dot(const Vec3& r) const noexcept { return x * r.x + y * r.y + z * r.z; }

		constexpr Vec3	Cross(const Vec3& r) const noexcept
		{
			return Vec3(
				y * r.z - z * r.y,
				z * r.x - x * r.z,
				x * r.y - y * r.x
			);
		}

		float MagnitudeSquared() const noexcept { return x * x + y * y + z * z; }
		float Magnitude()		 const noexcept { return std::sqrt(MagnitudeSquared()); }

		float DistanceSquared(const Vec3& r) const noexcept { return (*this - r).MagnitudeSquared(); }
		float Distance(const Vec3& r)		 const noexcept { return std::sqrt(DistanceSquared(r)); }

		bool  IsFinite() const noexcept { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
		bool  IsZero(float eps = EPSILON) const noexcept { return std::fabs(x) <= eps && std::fabs(y) <= eps && std::fabs(z) <= eps; }

		Vec3 GetNormalized(float eps = EPSILON) const noexcept
		{
			const float m2 = MagnitudeSquared();
			if (m2 <= eps * eps)
				return Vec3::Zero();

			const float inv = 1.0f / std::sqrt(m2);
			return (*this) * inv;
		}

		void Normalize(float eps = EPSILON) noexcept
		{
			*this = GetNormalized(eps);
		}

		Vec3 GetClamped(float maxLength) const noexcept
		{
			const float m = Magnitude();
			if (m <= maxLength || m <= EPSILON)
				return *this;
			return (*this) * (maxLength / m);
		}

		static Vec3 Lerp(const Vec3& a, const Vec3& b, float t) noexcept
		{
			t = Saturate(t);
			return a + (b - a) * t;
		}
	};
	inline constexpr Vec3 operator*(float s, const Vec3& v) noexcept { return v * s; }


	struct Quat
	{
		float x{}, y{}, z{}, w{ 1.f };

		constexpr Quat() = default;
		constexpr Quat(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}

		static constexpr Quat Identity() noexcept { return Quat(0.f, 0.f, 0.f, 1.f); }

		// yaw/pitch -> quaternion (Yaw around Y, Pitch around X)
		// 필요 시 좌표계에 맞게 축/부호만 조정하면 됨.
		static Quat FromYawPitch(float yaw, float pitch)
		{
			const float hy = yaw * 0.5f;
			const float hp = pitch * 0.5f;

			const float cy = std::cos(hy);
			const float sy = std::sin(hy);
			const float cp = std::cos(hp);
			const float sp = std::sin(hp);

			// q = qYaw(Y) * qPitch(X)
			Quat q{};
			q.w = cy * cp;
			q.x = cy * sp;
			q.y = sy * cp;
			q.z = -sy * sp;
			return q;
		}


		constexpr bool operator==(const Quat&) const noexcept = default;

		float MagnitudeSquared() const noexcept { return x * x + y * y + z * z + w * w; }
		float Magnitude() const noexcept { return std::sqrt(MagnitudeSquared()); }
		bool  IsFinite() const noexcept { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w); }

		Quat  Conjugate() const noexcept { return Quat(-x, -y, -z, w); }

		Quat Inverse(float eps = EPSILON) const noexcept
		{
			const float m2 = MagnitudeSquared();
			if (m2 <= eps * eps)
				return Quat::Identity();

			const float inv = 1.0f / m2;
			const Quat c = Conjugate();
			return Quat(c.x * inv, c.y * inv, c.z * inv, c.w * inv);
		}

		void Normalize(float eps = EPSILON) noexcept
		{
			const float m2 = MagnitudeSquared();
			if (m2 <= eps * eps)
			{
				*this = Quat::Identity();
				return;
			}

			const float inv = 1.0f / std::sqrt(m2);
			x *= inv; y *= inv; z *= inv; w *= inv;
		}

		Quat GetNormalized(float eps = EPSILON) const noexcept
		{
			Quat q = *this;
			q.Normalize(eps);
			return q;
		}

		// Hamilton product (PxQuat와 동일 의미)
		Quat operator*(const Quat& r) const noexcept
		{
			return Quat(
				w * r.x + x * r.w + y * r.z - z * r.y,
				w * r.y - x * r.z + y * r.w + z * r.x,
				w * r.z + x * r.y - y * r.x + z * r.w,
				w * r.w - x * r.x - y * r.y - z * r.z
			);
		}

		// Rotate vector: q * v * q^-1 (PxQuat::rotate와 동일 목적)
		Vec3 Rotate(const Vec3& v) const noexcept
		{
			const Quat q = GetNormalized();
			const Vec3 qv(q.x, q.y, q.z);
			const Vec3 t = 2.0f * qv.Cross(v);
			return v + (q.w * t) + qv.Cross(t);
		}

		static Quat Lerp(const Quat& a, const Quat& b, float t) noexcept
		{
			t = Saturate(t);
			Quat out(
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t,
				a.w + (b.w - a.w) * t
			);
			out.Normalize();
			return out;
		}
	};

	struct Transform
	{
		Vec3		p = Vec3::Zero();
		Quat		q = Quat::Identity();

		bool operator==(const Transform&) const = default;
	};

	struct RigidState
	{
		Transform	pose{};
		Vec3		linVel = Vec3::Zero();
		Vec3		angVel = Vec3::Zero();

		bool operator==(const RigidState&) const = default;

		bool IsFinite() const noexcept
		{
			return pose.p.IsFinite() && pose.q.IsFinite() && linVel.IsFinite() && angVel.IsFinite();
		}
	};

	struct CharacterState
	{
		Vec3		pos				= Vec3::Zero();
		float		facingYaw		= 0.f;
		float		facingPitch		= 0.f;
		float		verticalSpeed	= 0.f;
		float		horizontalSpeed = 0.f;
		Vec2		moveDir			= Vec2::Zero();
		uint32_t	stateFlags		= 0;

		bool operator==(const CharacterState&) const = default;

		bool IsFinite() const noexcept
		{
			return pos.IsFinite() && std::isfinite(facingYaw) && std::isfinite(facingPitch) && std::isfinite(verticalSpeed) && std::isfinite(horizontalSpeed) && moveDir.IsFinite();
		}
	};

	enum eStateFlag : uint32_t
	{
		STATE_NONE = 0,
		STATE_IS_JUMPING = 1u << 0,
		STATE_IS_SPRINT = 1u << 1,
	};

	inline bool HasStateFlag(uint32_t flags, eStateFlag f) noexcept { return (flags & static_cast<uint32_t>(f)) != 0; }
	inline void SetStateFlag(uint32_t& flags, eStateFlag f) noexcept { flags |= static_cast<uint32_t>(f); }
	inline void ClearStateFlag(uint32_t& flags, eStateFlag f) noexcept { flags &= ~static_cast<uint32_t>(f); }


	enum class eActorType : uint8
	{
		None		= 0,
		Generic		= 1,
		Projectile	= 2,
		Character   = 3,
	};

	enum class eBodyType : uint8
	{
		Rigid,
		Character,
	};

	enum class eMotionType : uint8
	{
		None		= 0,
		Static		= 1,		// rigid
		Dynamic		= 2,		// rigid
		Kinematic	= 3,		// rigid
		CCT			= 4,		// character
		RemoteCCT	= 5,		// character
	};

	struct MotionFlag
	{
		enum Enum : uint32
		{
			// ---- common ----

			NONE							= 0,
			DISABLE_GRAVITY					= 1 << 0,

			// ---- dynamic ----

			ENABLE_CCD						= 1 << 1,

			LOCK_LINEAR_X					= 1 << 2,
			LOCK_LINEAR_Y					= 1 << 3,
			LOCK_LINEAR_Z					= 1 << 4,
			LOCK_ANGULAR_X					= 1 << 5,
			LOCK_ANGULAR_Y					= 1 << 6,
			LOCK_ANGULAR_Z					= 1 << 7,
		};

		using Flags = FlagsT<Enum>;
	};

	inline void HashAppend(jam::Fnv1a32& h, const MotionFlag::Flags& bodyFlag) noexcept
	{
		HashAppend(h, bodyFlag.bits());
	}

	struct PhysicsHandle
	{
		uint64_t value{};

		bool IsValid() const { return value != 0; }
		constexpr bool operator==(const PhysicsHandle& r) const noexcept = default;
	};

	struct PrefabKey
	{
		uint64_t value{};

		bool IsValid() const { return value != 0; }
		constexpr bool operator==(const PrefabKey&) const noexcept = default;
	};


	enum class eSpawnSource : uint8
	{
		Level = 0,
		Runtime = 1,
		Network = 2,
		Tool = 3,
	};

	struct SpawnOverrideMask
	{
		enum Enum
		{
			NONE			= 0,
			LINEAR_VEL		= 1 << 0,
			ANGULAR_VEL		= 1 << 1,
			LINEAR_DAMP		= 1 << 2,
			ANGULAR_DAMP	= 1 << 3,

			VIEW_YAW		= 1 << 8,
			VIEW_PITCH		= 1 << 9,
		};

		using Flag = FlagsT<Enum>;
	};


	struct RigidSpawnOverrides
	{
		SpawnOverrideMask::Flag mask = SpawnOverrideMask::NONE;

		Vec3				linearVelocity  = Vec3::Zero();
		Vec3				angularVelocity = Vec3::Zero();
		float				linearDamping   = 0.0f;
		float				angularDamping  = 0.0f;
	};

	struct CharacterSpawnOverrides
	{
		SpawnOverrideMask::Flag mask = SpawnOverrideMask::NONE;

		float				yaw   = 0.0f;
		float				pitch = 0.0f;
	};


	struct SpawnDesc
	{
		PrefabKey			prefab{};
		Transform			pose{};
		eSpawnSource		spawnSrc = eSpawnSource::Level;

		uint16				team = 0;
		uint8				part = 0;
		uint8				role = 0;

		std::variant<RigidSpawnOverrides, CharacterSpawnOverrides> overrides;

		bool IsRigid() const noexcept { return std::holds_alternative<RigidSpawnOverrides>(overrides); }
		bool IsCharacter() const noexcept { return std::holds_alternative<CharacterSpawnOverrides>(overrides); }
	};


	struct PhysicsState
	{
		std::variant<RigidState, CharacterState> state;

		bool IsRigid() const noexcept { return std::holds_alternative<RigidState>(state); }
		bool IsCharacter() const noexcept { return std::holds_alternative<CharacterState>(state); }
	};



	using ObjectId = uint32;
	static constexpr ObjectId INVALID_OBJ_ID = 0;

	/// @brief 입력 비트 플래그 (32bit로 최대 32개 입력 지원)
	enum eInputFlag : uint32_t
	{
		INPUT_NONE = 0,
		INPUT_FORWARD = 1 << 0,
		INPUT_BACKWARD = 1 << 1,
		INPUT_LEFT = 1 << 2,
		INPUT_RIGHT = 1 << 3,
		INPUT_CROUCH = 1 << 4,
		INPUT_PRONE = 1 << 5,
		INPUT_RUN = 1 << 6,
		INPUT_SPRINT = 1 << 7,
		INPUT_JUMP = 1 << 8,
		INPUT_DASH = 1 << 9,
	};

	inline bool HasInputFlag(uint32_t flags, eInputFlag f) noexcept { return (flags & static_cast<uint32_t>(f)) != 0; }
	inline void SetInputFlag(uint32_t& flags, eInputFlag f) noexcept { flags |= static_cast<uint32_t>(f); }
	inline void ClearInputFlag(uint32_t& flags, eInputFlag f) noexcept { flags &= ~static_cast<uint32_t>(f); }

	struct CharacterInput
	{
		uint32_t	inputFlags = 0;
		float		facingYaw = 0.f;
		float		facingPitch = 0.f;
	};





	enum class eProjectileKind : uint8
	{
		DYN_SIM,		// 0~30   m/s : PxRigidDynamic, PhysX simulate
		ANALYTIC,		// 30-300 m/s : Kinematic + Manual integration + Sweep CCD
		HITSCAN,		// 300+   m/s : Instant raycast
	};


	struct ProjectileSpawnDesc
	{
		eProjectileKind kind = eProjectileKind::DYN_SIM;

		PrefabKey prefab{};
		Transform pose{};
		Vec3 velocity = Vec3::Zero();
		float gravityScale = 1.f;
		float maxRange = 1000.f;
		uint16 teamId = 0;
	};

	struct HitscanResult
	{
		bool hit = false;
		Vec3 position = Vec3::Zero();
		Vec3 normal = Vec3::Zero();
		ObjectId hitId = INVALID_OBJ_ID;
	};




}
