#include "pch.h"
#include "Clock.h"

namespace jam::utils
{
	void Clock::Start(uint32 tickHz)
	{
		LARGE_INTEGER freq;
		::QueryPerformanceFrequency(&freq);
		m_qpcFreq = freq.QuadPart;

		m_tickHz = tickHz;
		m_tickInterval_ns = (m_tickHz > 0) ? (1'000'000'000ull / m_tickHz) : 0ull;

		const int64 q = ReadQpc();
		if (m_qpcAtBoot == 0)
			m_qpcAtBoot = q;

		m_qpcAtStart = q;
	}




	uint64 Clock::NowNs() const
	{
		return NowAbsNs();
	}

	uint64 Clock::NowUs() const
	{
		return NowAbsNs() / 1'000ull;
	}

	uint64 Clock::NowMs() const
	{
		return NowAbsNs() / 1'000'000ull;
	}

	uint64 Clock::NowSec() const
	{
		return NowAbsNs() / 1'000'000'000ull;
	}

	uint64 Clock::ElapsedNs() const
	{
		return ElapsedAbsNs();
	}

	uint64 Clock::ElapsedUs() const
	{
		return ElapsedAbsNs() / 1'000ull;
	}

	uint64 Clock::ElapsedMs() const
	{
		return ElapsedAbsNs() / 1'000'000ull;
	}

	uint64 Clock::ElapsedSec() const
	{
		return ElapsedAbsNs() / 1'000'000'000ull;
	}





	static inline FractionalTick ToFractionalTickU64(uint64 totalNs, uint64 stepNs)
	{
		if (stepNs == 0) 
			return { 0, 0.0 };
		const uint64_t tick = totalNs / stepNs;
		const uint64_t rem = totalNs % stepNs;
		const double   alpha = static_cast<double>(rem) / static_cast<double>(stepNs);
		return FractionalTick{ tick, alpha };
	}


	FractionalTick Clock::NowFractionalTick() const
	{
		return ToFractionalTickU64(NowAbsNs(), m_tickInterval_ns);
	}

	FractionalTick Clock::ElapsedFractionalTick() const
	{
		return ToFractionalTickU64(ElapsedAbsNs(), m_tickInterval_ns);
	}
}
