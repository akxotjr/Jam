#include "pch.h"
#include "jamnet/core/executor/ProcessMetrics.h"

#include <psapi.h>

namespace jam
{
	namespace
	{
		constexpr ULONG kSystemProcessorPerformanceInformation = 8;
		constexpr LONG kStatusInfoLengthMismatch = static_cast<LONG>(0xC0000004L);

		struct SystemProcessorPerformanceInformation
		{
			LARGE_INTEGER idleTime;
			LARGE_INTEGER kernelTime;
			LARGE_INTEGER userTime;
			LARGE_INTEGER dpcTime;
			LARGE_INTEGER interruptTime;
			ULONG		  interruptCount;
		};

		using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

		uint64 FileTimeValue(const FILETIME& value)
		{
			ULARGE_INTEGER result{};
			result.LowPart = value.dwLowDateTime;
			result.HighPart = value.dwHighDateTime;
			return result.QuadPart;
		}

		uint64 Percent(const uint64 busy, const uint64 total)
		{
			return total == 0 ? 0 : std::min<uint64>(100, (busy * 100 + total / 2) / total);
		}
	}

	void ProcessMetrics::Initialize(const uint32 logicalProcessorCount, const uint64 windowIndex)
	{
		m_logicalProcessorCount = std::max<uint32>(1, logicalProcessorCount);
		m_windowIndex = windowIndex;
		m_previousCoreTimes.reserve(m_logicalProcessorCount);
		ResetCpuBaseline();
	}


	void ProcessMetrics::SubmitCompletedWindow(const uint64 nowNs, MetricsAggregator& aggregator)
	{
		if (!aggregator.IsEnabled())
			return;

		const uint64 currentWindowIndex = aggregator.WindowIndex(nowNs);
		if (m_windowIndex == UINT64_MAX)
		{
			m_windowIndex = currentWindowIndex;
			ResetCpuBaseline();
			return;
		}
		if (currentWindowIndex == m_windowIndex)
			return;

		uint64 processTime = 0;
		std::vector<CpuTimes> coreTimes;
		if (!CaptureCpuTimes(processTime, coreTimes) || coreTimes.size() != m_previousCoreTimes.size())
		{
			m_windowIndex = currentWindowIndex;
			ResetCpuBaseline();
			return;
		}

		uint64 totalDelta = 0;
		for (size_t i = 0; i < coreTimes.size(); ++i)
			totalDelta += coreTimes[i].total - m_previousCoreTimes[i].total;

		PROCESS_MEMORY_COUNTERS_EX memory{};
		memory.cb = sizeof(memory);
		K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));

		const uint64 periodNs = aggregator.WindowPeriodNs();
		const uint64 startNs  = m_windowIndex * periodNs;
		const uint64 endNs	  = startNs + periodNs;
		const uint64 processDelta = processTime - m_previousProcessTime;

		MetricSnapshot process{
			m_windowIndex, startNs, endNs, "process", 0, 0,
			{
				{ "process_cpu_usage", Percent(processDelta, totalDelta), eMetricAggregation::Latest },
				{ "working_set_bytes", memory.WorkingSetSize, eMetricAggregation::Latest },
				{ "private_bytes", memory.PrivateUsage, eMetricAggregation::Latest },
			}
		};
		aggregator.Submit(std::move(process));

		for (uint32 i = 0; i < static_cast<uint32>(coreTimes.size()); ++i)
		{
			const uint64 coreTotalDelta = coreTimes[i].total - m_previousCoreTimes[i].total;
			const uint64 coreIdleDelta = coreTimes[i].idle - m_previousCoreTimes[i].idle;
			const uint64 busyDelta = coreTotalDelta >= coreIdleDelta ? coreTotalDelta - coreIdleDelta : 0;
			MetricSnapshot core{ m_windowIndex, startNs, endNs, "process", 0, i,
				{ { "core_cpu_usage", Percent(busyDelta, coreTotalDelta), eMetricAggregation::Latest } } };
			aggregator.Submit(std::move(core));
		}

		m_windowIndex = currentWindowIndex;
		m_previousProcessTime = processTime;
		m_previousCoreTimes = std::move(coreTimes);
	}

	bool ProcessMetrics::CaptureCpuTimes(uint64& processTime, std::vector<CpuTimes>& coreTimes) const
	{
		FILETIME creation{}, exit{}, kernel{}, user{};
		if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
			return false;
		
		processTime = FileTimeValue(kernel) + FileTimeValue(user);

		const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		const auto query = ntdll ? reinterpret_cast<NtQuerySystemInformationFn>(GetProcAddress(ntdll, "NtQuerySystemInformation")) : nullptr;
		if (!query)
			return false;

		std::vector<SystemProcessorPerformanceInformation> info(m_logicalProcessorCount);
		ULONG returnedLength = 0;
		LONG status = query(kSystemProcessorPerformanceInformation, info.data(), static_cast<ULONG>(info.size() * sizeof(info[0])), &returnedLength);

		if (status == kStatusInfoLengthMismatch && returnedLength > info.size() * sizeof(info[0]))
		{
			info.resize((returnedLength + sizeof(info[0]) - 1) / sizeof(info[0]));
			status = query(kSystemProcessorPerformanceInformation, info.data(), static_cast<ULONG>(info.size() * sizeof(info[0])), &returnedLength);
		}

		if (status < 0)
			return false;

		const size_t count = returnedLength / sizeof(info[0]);
		coreTimes.resize(count);

		for (size_t i = 0; i < count; ++i)
		{
			coreTimes[i].idle = static_cast<uint64>(info[i].idleTime.QuadPart);
			coreTimes[i].total = static_cast<uint64>(info[i].kernelTime.QuadPart + info[i].userTime.QuadPart);
		}
		
		return !coreTimes.empty();
	}

	void ProcessMetrics::ResetCpuBaseline()
	{
		CaptureCpuTimes(m_previousProcessTime, m_previousCoreTimes);
	}
}
