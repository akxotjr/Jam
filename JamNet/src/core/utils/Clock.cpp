#include "pch.h"
#include "jamnet/core/utils/Clock.h"

namespace jam
{
	namespace 
	{
		FractionalTick ToFractionalTickU64(const uint64 totalNs, const uint64 stepNs)
		{
			if (stepNs == 0) return { 0, 0.0 };

			const uint64_t tick  = totalNs / stepNs;
			const uint64_t rem   = totalNs % stepNs;
			const double   alpha = static_cast<double>(rem) / static_cast<double>(stepNs);

			return FractionalTick{ tick, alpha };
		}

	} // anonymous namespace


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


	FractionalTick Clock::NowFractionalTick() const
	{
		return ToFractionalTickU64(NowAbsNs(), m_tickInterval_ns);
	}

	FractionalTick Clock::ElapsedFractionalTick() const
	{
		return ToFractionalTickU64(ElapsedAbsNs(), m_tickInterval_ns);
	}

	int64 Clock::ReadQpc() const
	{
		LARGE_INTEGER counter;
		::QueryPerformanceCounter(&counter);
		return counter.QuadPart;
	}

	uint64 Clock::QpcToNs(int64 counter) const
	{
		assert(counter >= 0);              
		const uint64 c = static_cast<uint64>(counter);
		const uint64 f = static_cast<uint64>(m_qpcFreq);

		uint64 hi;
		const uint64 lo = _umul128(c, 1'000'000'000ULL, &hi);
		return _udiv128(hi, lo, f, nullptr);
	}

	uint64 Clock::NowAbsNs() const
	{
		return QpcToNs(ReadQpc() - m_qpcAtBoot);
	}

	uint64 Clock::ElapsedAbsNs() const
	{
		return QpcToNs(ReadQpc() - m_qpcAtStart);
	}
}
