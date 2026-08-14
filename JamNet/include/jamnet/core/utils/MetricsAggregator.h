#pragma once

#include <jambase/JamTypes.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace jam
{
	enum class eMetricAggregation : uint8
	{
		Sum,
		Maximum,
		Latest,
	};

	struct MetricValue
	{
		std::string			name;
		uint64				value		= 0;
		eMetricAggregation	aggregation = eMetricAggregation::Sum;
	};

	struct MetricHistogramConfig
	{
		uint64 lowestTrackableValue  = 1;
		uint64 highestTrackableValue = 1;
		uint8  significantDigits     = 3;
		std::string unit;

		bool operator==(const MetricHistogramConfig&) const = default;
	};

	class MetricHistogram
	{
	public:
		MetricHistogram();
		MetricHistogram(std::string name, MetricHistogramConfig config);
		~MetricHistogram();

		MetricHistogram(MetricHistogram&&) noexcept;
		MetricHistogram& operator=(MetricHistogram&&) noexcept;
		MetricHistogram(const MetricHistogram&) = delete;
		MetricHistogram& operator=(const MetricHistogram&) = delete;

		bool							Record(uint64 value, uint64 count = 1);
		bool							IsValid() const;
		const std::string&				Name() const { return m_name; }
		const MetricHistogramConfig&	Config() const { return m_config; }
		uint64							OverflowCount() const { return m_overflowCount; }

	private:
		struct Impl;
		bool							MergeFrom(MetricHistogram&& other);

	private:

		std::string				m_name;
		MetricHistogramConfig	m_config = {};
		std::unique_ptr<Impl>	m_impl;
		uint64					m_overflowCount = 0;

		friend class MetricsAggregator;
	};

	struct MetricSnapshot
	{
		uint64						windowIndex		= 0;
		uint64						windowStartNs	= 0;
		uint64						windowEndNs		= 0;
		std::string					scope;
		uint32						shardIndex		= 0;
		uint64						sourceId		= 0;
		std::vector<MetricValue>	values;
		std::vector<MetricHistogram> histograms;

		bool IsValid() const { return !scope.empty() && (!values.empty() || !histograms.empty()) && windowEndNs >= windowStartNs; }
	};

	struct MetricsAggregatorConfig
	{
		bool					enabled				= false;
		uint64					windowPeriodNs		= 5_s;
		std::filesystem::path	outputDirectory		= "logs/metrics";
		std::string				fileName;
		std::string				histogramFileName;
	};

	class MetricsAggregator
	{
	public:
		MetricsAggregator();
		~MetricsAggregator();

		bool	Initialize(MetricsAggregatorConfig config);
		void	Submit(MetricSnapshot snapshot);
		void	Flush();
		void	Shutdown();

		uint64	WindowIndex(uint64 nowNs) const;
		uint64	WindowPeriodNs() const { return m_config.windowPeriodNs; }
		bool	IsEnabled() const { return m_config.enabled; }

	private:
		struct HistogramLogState;

		struct MetricKey
		{
			std::string scope;
			uint32		shardIndex = 0;
			uint64		sourceId = 0;
			std::string name;

			bool operator==(const MetricKey&) const = default;
		};

		struct MetricKeyHash
		{
			size_t operator()(const MetricKey& key) const;
		};

		struct AggregatedValue
		{
			uint64				value = 0;
			eMetricAggregation	aggregation = eMetricAggregation::Sum;
		};

		struct Window
		{
			uint64 startNs = 0;
			uint64 endNs   = 0;
			std::unordered_map<MetricKey, AggregatedValue, MetricKeyHash> values;
			std::unordered_map<MetricKey, MetricHistogram, MetricKeyHash> histograms;
		};

		void FlushCompletedWindows(uint64 newestWindowIndex);
		void WriteWindow(uint64 windowIndex, const Window& window);
		bool EnsureOutputOpen();
		void WriteHistogramLog(uint64 windowIndex, const Window& window);

		static std::string_view AggregationName(eMetricAggregation aggregation);

	private:
		mutable std::mutex					m_mutex;
		MetricsAggregatorConfig				m_config = {};
		std::unordered_map<uint64, Window>	m_windows;
		std::ofstream						m_output;
		std::unique_ptr<HistogramLogState>	m_histogramLog;
		uint64								m_startUnixNs = 0;
		uint64								m_startMonotonicNs = 0;
	};
}
