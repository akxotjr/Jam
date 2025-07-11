#include "pch.h"
#include "Clock.h"

namespace jam::net
{
	void Clock::Start(double tickInterval)
	{
		m_tickInterval = tickInterval;

		QueryPerformanceFrequency(&m_frequency);
		QueryPerformanceCounter(&m_start);
	}

	double Clock::GetElapsedSeconds() const
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return static_cast<double>(now.QuadPart - m_start.QuadPart) / m_frequency.QuadPart;
	}

	double Clock::GetElapsedMiliseconds() const
	{
		return GetElapsedSeconds() * 1000.0;
	}

	uint64 Clock::GetCurrentTick() const
	{
		double elapsed = GetElapsedSeconds();
		return static_cast<uint64_t>(elapsed / m_tickInterval);
	}

	FractionalTick Clock::GetFractionalTick() const
	{
		double elapsed = GetElapsedSeconds();
		double exactTick = elapsed / m_tickInterval;

		FractionalTick result;
		result.tick = static_cast<uint64>(exactTick);
		result.alpha = exactTick - result.tick;
		return result;
	}
}
