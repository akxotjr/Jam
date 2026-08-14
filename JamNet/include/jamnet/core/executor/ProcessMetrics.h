#pragma once

#include "jamnet/core/utils/MetricsAggregator.h"

#include <atomic>
#include <vector>

namespace jam
{
	class ProcessMetrics
	{
	public:
		void Initialize(uint32 logicalProcessorCount, uint64 windowIndex);
		void SubmitCompletedWindow(uint64 nowNs, MetricsAggregator& aggregator);

	private:
		struct CpuTimes
		{
			uint64 idle = 0;
			uint64 total = 0;
		};

		bool CaptureCpuTimes(uint64& processTime, std::vector<CpuTimes>& coreTimes) const;
		void ResetCpuBaseline();

	private:
		uint32					m_logicalProcessorCount = 1;
		uint64					m_windowIndex			= UINT64_MAX;
		uint64					m_previousProcessTime	= 0;
		std::vector<CpuTimes>	m_previousCoreTimes;
	};
}
