#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>


namespace jam::px
{
	struct EaseProfile
	{
		float easeInTime	= 0.2f;
		float easeOutTime	= 0.2f;
	};

	enum class eEaseType : uint8_t
	{
		Linear,
		SmoothStep,
		SmootherStep,

		InSine,
		OutSine,
		InOutSine,

		InQuad,
		OutQuad,
		InOutQuad,

		InCubic,
		OutCubic,
		InOutCubic
	};

	class Ease
	{
	public:
		//--------------------------------------------------------------------------
		// Basic helpers
		//--------------------------------------------------------------------------

		static float Clamp01(float t) noexcept
		{
			return std::clamp(t, 0.0f, 1.0f);
		}

		static float SafeRatio(float num, float den) noexcept
		{
			return (den > 0.0f) ? (num / den) : 0.0f;
		}

		//--------------------------------------------------------------------------
		// Easing functions: input t in [0,1], output eased t in [0,1]
		//--------------------------------------------------------------------------

		static float Linear(float t) noexcept
		{
			return Clamp01(t);
		}

		static float SmoothStep(float t) noexcept
		{
			t = Clamp01(t);
			return t * t * (3.0f - 2.0f * t);
		}

		static float SmootherStep(float t) noexcept
		{
			t = Clamp01(t);
			return t * t * t * (t * (6.0f * t - 15.0f) + 10.0f);
		}

		static float InSine(float t) noexcept
		{
			t = Clamp01(t);
			return 1.0f - std::cos(physx::PxPiDivTwo * t);
		}

		static float OutSine(float t) noexcept
		{
			t = Clamp01(t);
			return std::sin(physx::PxPiDivTwo * t);
		}

		static float InOutSine(float t) noexcept
		{
			t = Clamp01(t);
			return 0.5f * (1.0f - std::cos(physx::PxPi * t));
		}

		static float InQuad(float t) noexcept
		{
			t = Clamp01(t);
			return t * t;
		}

		static float OutQuad(float t) noexcept
		{
			t = Clamp01(t);
			return 1.0f - (1.0f - t) * (1.0f - t);
		}

		static float InOutQuad(float t) noexcept
		{
			t = Clamp01(t);
			if (t < 0.5f)
				return 2.0f * t * t;

			const float u = 1.0f - t;
			return 1.0f - 2.0f * u * u;
		}

		static float InCubic(float t) noexcept
		{
			t = Clamp01(t);
			return t * t * t;
		}

		static float OutCubic(float t) noexcept
		{
			t = Clamp01(t);
			const float u = 1.0f - t;
			return 1.0f - u * u * u;
		}

		static float InOutCubic(float t) noexcept
		{
			t = Clamp01(t);
			if (t < 0.5f)
				return 4.0f * t * t * t;

			const float u = 1.0f - t;
			return 1.0f - 4.0f * u * u * u;
		}

		static float Evaluate(eEaseType type, float t) noexcept
		{
			switch (type)
			{
			case eEaseType::Linear:        return Linear(t);
			case eEaseType::SmoothStep:    return SmoothStep(t);
			case eEaseType::SmootherStep:  return SmootherStep(t);

			case eEaseType::InSine:        return InSine(t);
			case eEaseType::OutSine:       return OutSine(t);
			case eEaseType::InOutSine:     return InOutSine(t);

			case eEaseType::InQuad:        return InQuad(t);
			case eEaseType::OutQuad:       return OutQuad(t);
			case eEaseType::InOutQuad:     return InOutQuad(t);

			case eEaseType::InCubic:       return InCubic(t);
			case eEaseType::OutCubic:      return OutCubic(t);
			case eEaseType::InOutCubic:    return InOutCubic(t);
			default:                      return Linear(t);
			}
		}

		//--------------------------------------------------------------------------
		// EaseProfile
		//
		// totalTime 동안 진행되는 이동/회전 등에 대해,
		// 앞쪽 easeInTime 구간은 가속,
		// 뒤쪽 easeOutTime 구간은 감속,
		// 중간은 선형 속도에 가깝게 유지되도록
		// normalized progress t=[0,1] 를 재매핑한다.
		//
		// 반환값도 [0,1].
		//--------------------------------------------------------------------------

		static float ApplyProfile(float t, float totalTime, const EaseProfile& profile) noexcept
		{
			t = Clamp01(t);

			if (totalTime <= 0.0f)
				return t;

			float inN = profile.easeInTime / totalTime;
			float outN = profile.easeOutTime / totalTime;

			inN = std::max(0.0f, inN);
			outN = std::max(0.0f, outN);

			// 합이 1을 넘으면 비율 유지하면서 압축
			const float sum = inN + outN;
			if (sum > 1.0f && sum > 0.0f)
			{
				const float s = 1.0f / sum;
				inN *= s;
				outN *= s;
			}

			const float midN = 1.0f - inN - outN;

			// 전부 0이면 선형
			if (inN <= 0.0f && outN <= 0.0f)
				return t;

			// ease-in only
			if (outN <= 0.0f)
			{
				if (t < inN)
				{
					const float u = SafeRatio(t, inN);
					return inN * SmoothStep(u);
				}
				return t;
			}

			// ease-out only
			if (inN <= 0.0f)
			{
				const float outStart = 1.0f - outN;
				if (t > outStart)
				{
					const float u = SafeRatio(t - outStart, outN);
					return outStart + outN * SmoothStep(u);
				}
				return t;
			}

			// ease-in zone
			if (t < inN)
			{
				const float u = SafeRatio(t, inN);
				return inN * SmoothStep(u);
			}

			// middle zone
			const float outStart = 1.0f - outN;
			if (t <= outStart)
			{
				if (midN <= 0.0f)
					return t;

				return t;
			}

			// ease-out zone
			{
				const float u = SafeRatio(t - outStart, outN);
				return outStart + outN * SmoothStep(u);
			}
		}

		//--------------------------------------------------------------------------
		// Generic lerp
		//--------------------------------------------------------------------------

		template <typename T>
		static T Lerp(const T& a, const T& b, float t) noexcept
		{
			t = Clamp01(t);
			return a + (b - a) * t;
		}

		static float Lerp(float a, float b, float t) noexcept
		{
			t = Clamp01(t);
			return a + (b - a) * t;
		}

		static physx::PxVec3 Lerp(const physx::PxVec3& a, const physx::PxVec3& b, float t) noexcept
		{
			t = Clamp01(t);
			return a + (b - a) * t;
		}

		// Quaternion은 선형보간보다 slerp가 더 적절
		static physx::PxQuat Slerp(const physx::PxQuat& a, const physx::PxQuat& b, float t) noexcept
		{
			t = Clamp01(t);
			return physx::PxSlerp(t, a, b);
		}

		//--------------------------------------------------------------------------
		// Lerp + easing
		//--------------------------------------------------------------------------

		template <typename T>
		static T LerpEase(const T& a, const T& b, float t, eEaseType easeType) noexcept
		{
			return Lerp(a, b, Evaluate(easeType, t));
		}

		template <typename T>
		static T LerpSmoothStep(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, SmoothStep(t));
		}

		template <typename T>
		static T LerpSmootherStep(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, SmootherStep(t));
		}

		template <typename T>
		static T LerpInSine(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, InSine(t));
		}

		template <typename T>
		static T LerpOutSine(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, OutSine(t));
		}

		template <typename T>
		static T LerpInOutSine(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, InOutSine(t));
		}

		template <typename T>
		static T LerpInQuad(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, InQuad(t));
		}

		template <typename T>
		static T LerpOutQuad(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, OutQuad(t));
		}

		template <typename T>
		static T LerpInOutQuad(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, InOutQuad(t));
		}

		template <typename T>
		static T LerpInCubic(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, InCubic(t));
		}

		template <typename T>
		static T LerpOutCubic(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, OutCubic(t));
		}

		template <typename T>
		static T LerpInOutCubic(const T& a, const T& b, float t) noexcept
		{
			return Lerp(a, b, InOutCubic(t));
		}

		//--------------------------------------------------------------------------
		// Duration-based helpers
		//
		// elapsedTime / totalTime 기반으로 직접 보간하고 싶을 때 사용.
		//--------------------------------------------------------------------------

		template <typename T>
		static T LerpByDuration(const T& a, const T& b, float elapsedTime, float totalTime, eEaseType easeType) noexcept
		{
			if (totalTime <= 0.0f)
				return b;

			const float t = Clamp01(elapsedTime / totalTime);
			return Lerp(a, b, Evaluate(easeType, t));
		}

		template <typename T>
		static T LerpByProfile(const T& a, const T& b, float elapsedTime, float totalTime, const EaseProfile& profile) noexcept
		{
			if (totalTime <= 0.0f)
				return b;

			const float t = Clamp01(elapsedTime / totalTime);
			return Lerp(a, b, ApplyProfile(t, totalTime, profile));
		}

		static physx::PxQuat SlerpByDuration(const physx::PxQuat& a, const physx::PxQuat& b, float elapsedTime, float totalTime, eEaseType easeType) noexcept
		{
			if (totalTime <= 0.0f)
				return b;

			const float t = Clamp01(elapsedTime / totalTime);
			return Slerp(a, b, Evaluate(easeType, t));
		}

		static physx::PxQuat SlerpByProfile(const physx::PxQuat& a, const physx::PxQuat& b, float elapsedTime, float totalTime, const EaseProfile& profile) noexcept
		{
			if (totalTime <= 0.0f)
				return b;

			const float t = Clamp01(elapsedTime / totalTime);
			return Slerp(a, b, ApplyProfile(t, totalTime, profile));
		}
	};
}