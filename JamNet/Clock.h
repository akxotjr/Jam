#pragma once

namespace jam::net
{
	struct FractionalTick
	{
		uint64 tick;     // 정수 틱
		double alpha;    // 0.0 ~ 1.0 사이의 진행도
	};

	class Clock
	{
		DECLARE_SINGLETON(Clock)

	public:
		void				Start(double tickInterval);

		double				GetElapsedSeconds() const; 
		double				GetElapsedMiliseconds() const;
		uint64				GetCurrentTick() const;
		FractionalTick		GetFractionalTick() const;

	private:
		LARGE_INTEGER		m_frequency;
		LARGE_INTEGER		m_start;
		double				m_tickInterval;
	};
}

