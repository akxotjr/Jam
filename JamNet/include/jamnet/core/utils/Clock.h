#pragma once
#include "jamnet/core/utils/TimeUnits.h"

#include <chrono>


namespace jam
{
	struct FractionalTick
	{
		uint64 tick		= 0;
		double alpha	= 0.0;    // [0, 1)
	};

	class Clock
	{
		DECLARE_SINGLETON(Clock)

	public:
		void			Start(uint32 tickHz);

		// 절대 단조 시간(부트 원점 기준)
		uint64			NowNs()  const;
		uint64			NowUs()  const;
		uint64			NowMs()  const;
		uint64			NowSec() const;

		// Start() 이후 경과
		uint64			ElapsedNs()  const;
		uint64			ElapsedUs()  const;
		uint64			ElapsedMs()  const;
		uint64			ElapsedSec() const;


		FractionalTick	NowFractionalTick() const;
		FractionalTick	ElapsedFractionalTick() const;


		template<class Dur>
		Dur NowChrono() const
		{
			return std::chrono::duration_cast<Dur>(std::chrono::nanoseconds(NowNs()));
		}
		template<class Dur>
		Dur ElapsedChrono() const
		{
			return std::chrono::duration_cast<Dur>(std::chrono::nanoseconds(ElapsedNs()));
		}

	private:

		int64			ReadQpc() const;
		uint64			QpcToNs(int64 counter) const;
		uint64			NowAbsNs() const;
		uint64			ElapsedAbsNs() const;

	private:

		int64	m_qpcFreq		  = 0;
		int64	m_qpcAtBoot		  = 0;
		int64	m_qpcAtStart	  = 0;

		uint32	m_tickHz		  = 0;
		uint64	m_tickInterval_ns = 0_ns;
	};
}


#define CLOCK							jam::Clock::Instance()
#define NOW_NS()						jam::Clock::Instance().NowNs()
#define NOW_US()						jam::Clock::Instance().NowUs()
#define NOW_MS()						jam::Clock::Instance().NowMs()
#define NOW_SEC()						jam::Clock::Instance().NowSec()
#define ELAPSED_NS()					jam::Clock::Instance().ElapsedNs()
#define ELAPSED_US()					jam::Clock::Instance().ElapsedUs()
#define ELAPSED_MS()					jam::Clock::Instance().ElapsedMs()
#define ELAPSED_SEC()					jam::Clock::Instance().ElapsedSec()
#define NOW_FRACTIONAL_TICK()			jam::Clock::Instance().NowFractionalTick()
#define ELAPSED_FRACTIONAL_TICK()		jam::Clock::Instance().ElapsedFractionalTick()
#define NOW_CHRONO(Dur)					jam::Clock::Instance().NowChrono<Dur>()
#define ELAPSED_CHRONO(Dur)				jam::Clock::Instance().ElapsedChrono<Dur>()
