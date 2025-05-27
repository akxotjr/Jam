#include "pch.h"
#include "TimeManager.h"

namespace jam::utils
{
	void TimeManager::Init()
	{
		::QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&m_frequency));
		::QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&m_prevCount));
	}

	void TimeManager::Shutdown()
	{
	}

	void TimeManager::Update()
	{
		uint64 currentCount;
		::QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentCount));

		m_deltaTime = static_cast<double>(currentCount - m_prevCount) / static_cast<double>(m_frequency);
		m_serverTime += m_deltaTime;
		m_prevCount = currentCount;
	}
}