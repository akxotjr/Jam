#pragma once
#include "Clock.h"


namespace jam::utils::exec
{
	struct RouteSeed
	{
		uint64 k0;
		uint64 k1;
	};

	inline uint64 Mix64(uint64 x)
	{
		x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
		x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
		x ^= x >> 33; return x;
	}

	// based on current time
	inline RouteSeed RandomSeed()
	{
		const uint64 now = Clock::Instance().NowNs();
		return { .k0 = now ^ 0x9e3779b97f4a7c15ULL, .k1= ~now };
	}

	class RoutingKey
	{
	public:
		explicit RoutingKey(RouteSeed s) : m_seed(s) {}

		// PerUser 기본
		uint64 KeyForSession(uint64 userId) const { return Mix64(userId ^ m_seed.k0); }

		// 그룹성 작업 오버라이드에 사용
		uint64 KeyForGroup(uint64 groupId) const { return Mix64(groupId ^ m_seed.k1); }

	private:
		RouteSeed m_seed;
	};
}

