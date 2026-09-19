#include "pch.h"
#include "jamnet/core/utils/Clock.h"

namespace jam
{
	namespace 
	{
		FractionalTick ToFractionalTick(Duration total, Duration step)
		{
			if (step.IsZero()) return { 0, 0.0 };

			const uint64 tick  = total / step;
			const uint64 rem   = total.Ns() % step.Ns();
			const double alpha = static_cast<double>(rem) / static_cast<double>(step.Ns());

			return FractionalTick{ tick, alpha };
		}

	} // anonymous namespace


	void Clock::Start(uint32 tickHz)
	{
		LARGE_INTEGER freq;
		::QueryPerformanceFrequency(&freq);
		m_qpcFreq = freq.QuadPart;

		m_tickHz = tickHz;
		m_tickInterval = (m_tickHz > 0)
			? Duration::FromNs(Duration::NsPerSec / m_tickHz)
			: Duration::Zero();

		const int64 q = ReadQpc();
		if (m_qpcAtBoot == 0)
			m_qpcAtBoot = q;

		m_qpcAtStart = q;
	}

	Timestamp Clock::Now() const
	{
		return NowAbs();
	}

	Duration Clock::Elapsed() const
	{
		return ElapsedAbs();
	}


	FractionalTick Clock::NowFractionalTick() const
	{
		return ToFractionalTick(Duration::FromNs(NowAbs().Ns()), m_tickInterval);
	}

	FractionalTick Clock::ElapsedFractionalTick() const
	{
		return ToFractionalTick(ElapsedAbs(), m_tickInterval);
	}

	int64 Clock::ReadQpc() const
	{
		LARGE_INTEGER counter;
		::QueryPerformanceCounter(&counter);
		return counter.QuadPart;
	}

	Duration Clock::QpcToDuration(int64 counter) const
	{
		assert(counter >= 0);              
		const uint64 c = static_cast<uint64>(counter);
		const uint64 f = static_cast<uint64>(m_qpcFreq);

		uint64 hi;
		const uint64 lo = _umul128(c, 1'000'000'000ULL, &hi);
		return Duration::FromNs(_udiv128(hi, lo, f, nullptr));
	}

	Timestamp Clock::NowAbs() const
	{
		return Timestamp::FromNs(QpcToDuration(ReadQpc() - m_qpcAtBoot).Ns());
	}

	Duration Clock::ElapsedAbs() const
	{
		return QpcToDuration(ReadQpc() - m_qpcAtStart);
	}
}
