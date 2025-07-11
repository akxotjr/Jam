#pragma once

namespace jam::net
{
	struct FractionalTick
	{
		uint64 tick;     // ���� ƽ
		double alpha;    // 0.0 ~ 1.0 ������ ���൵
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

