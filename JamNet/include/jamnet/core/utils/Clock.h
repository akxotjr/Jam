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
		Timestamp		Now() const;

		// Start() 이후 경과
		Duration		Elapsed() const;


		FractionalTick	NowFractionalTick() const;
		FractionalTick	ElapsedFractionalTick() const;


		template<class Dur>
		Dur NowChrono() const
		{
			return std::chrono::duration_cast<Dur>(std::chrono::nanoseconds(Now().Ns()));
		}
		template<class Dur>
		Dur ElapsedChrono() const
		{
			return std::chrono::duration_cast<Dur>(std::chrono::nanoseconds(Elapsed().Ns()));
		}

	private:

		int64			ReadQpc() const;
		Duration		QpcToDuration(int64 counter) const;
		Timestamp		NowAbs() const;
		Duration		ElapsedAbs() const;

	private:

		int64	m_qpcFreq		  = 0;
		int64	m_qpcAtBoot		  = 0;
		int64	m_qpcAtStart	  = 0;

		uint32	m_tickHz		  = 0;
		Duration m_tickInterval = Duration::Zero();
	};
}


#define CLOCK							jam::Clock::Instance()
#define NOW()							jam::Clock::Instance().Now()
#define ELAPSED()						jam::Clock::Instance().Elapsed()
#define NOW_FRACTIONAL_TICK()			jam::Clock::Instance().NowFractionalTick()
#define ELAPSED_FRACTIONAL_TICK()		jam::Clock::Instance().ElapsedFractionalTick()
#define NOW_CHRONO(Dur)					jam::Clock::Instance().NowChrono<Dur>()
#define ELAPSED_CHRONO(Dur)				jam::Clock::Instance().ElapsedChrono<Dur>()
