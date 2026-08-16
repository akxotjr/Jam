#include "pch.h"
#include "jamnet/core/utils/MetricsAggregator.h"

#include <jambase/Logger.h>

#include <hdr/hdr_histogram.h>
#include <hdr/hdr_histogram_log.h>

#include <cerrno>
#include <cstdio>
#include <chrono>

namespace jam
{
	struct MetricHistogram::Impl
	{
		hdr_histogram* histogram = nullptr;

		~Impl()
		{
			if (histogram)
				hdr_close(histogram);
		}
	};

	MetricHistogram::MetricHistogram() = default;

	MetricHistogram::MetricHistogram(std::string name, MetricHistogramConfig config)
		: m_name(std::move(name)), m_config(std::move(config)), m_impl(std::make_unique<Impl>())
	{
		if (m_name.empty() || m_config.lowestTrackableValue == 0 ||
			m_config.highestTrackableValue < m_config.lowestTrackableValue ||
			m_config.significantDigits < 1 || m_config.significantDigits > 5)
		{
			m_impl.reset();
			return;
		}

		if (hdr_init(
			static_cast<int64_t>(m_config.lowestTrackableValue),
			static_cast<int64_t>(m_config.highestTrackableValue),
			m_config.significantDigits,
			&m_impl->histogram) != 0)
		{
			m_impl.reset();
		}
	}

	MetricHistogram::~MetricHistogram() = default;
	MetricHistogram::MetricHistogram(MetricHistogram&&) noexcept = default;
	MetricHistogram& MetricHistogram::operator=(MetricHistogram&&) noexcept = default;

	bool MetricHistogram::Record(const uint64 value, const uint64 count)
	{
		if (!IsValid() || count == 0)
			return false;
		if (value > m_config.highestTrackableValue ||
			!hdr_record_values(m_impl->histogram, static_cast<int64_t>(value), static_cast<int64_t>(count)))
		{
			m_overflowCount += count;
			return false;
		}
		return true;
	}

	bool MetricHistogram::IsValid() const
	{
		return m_impl && m_impl->histogram;
	}

	bool MetricHistogram::MergeFrom(MetricHistogram&& other)
	{
		if (!IsValid() || !other.IsValid() || m_config != other.m_config)
			return false;

		const int64_t dropped = hdr_add(m_impl->histogram, other.m_impl->histogram);
		m_overflowCount += other.m_overflowCount + (dropped > 0 ? static_cast<uint64>(dropped) : 0);
		return dropped == 0;
	}




	struct MetricsAggregator::HistogramLogState
	{
		FILE*			file   = nullptr;
		hdr_log_writer	writer = {};

		~HistogramLogState()
		{
			if (file)
				std::fclose(file);
		}
	};

	MetricsAggregator::MetricsAggregator() = default;

	MetricsAggregator::~MetricsAggregator()
	{
		Shutdown();
	}

	bool MetricsAggregator::Initialize(MetricsAggregatorConfig config)
	{
		std::scoped_lock lock(m_mutex);
		if (config.windowPeriodNs == 0)
			return false;

		m_config = std::move(config);
		m_windows.clear();
		m_startUnixNs = 0;
		m_startMonotonicNs = 0;

		if (m_output.is_open())
			m_output.close();
		m_histogramLog.reset();

		if (!m_config.enabled)
			return true;

		if (m_config.fileName.empty() || m_config.histogramFileName.empty())
		{
			const std::string stem = detail::TimestampedStem();
			if (m_config.fileName.empty())
				m_config.fileName = stem + ".csv";
			if (m_config.histogramFileName.empty())
				m_config.histogramFileName = stem + ".hlog";
		}

		return EnsureOutputOpen();
	}

	void MetricsAggregator::Submit(MetricSnapshot snapshot)
	{
		if (!snapshot.IsValid())
			return;

		std::scoped_lock lock(m_mutex);
		if (!m_config.enabled)
			return;

		FlushCompletedWindows(snapshot.windowIndex);
		Window& window = m_windows[snapshot.windowIndex];
		window.startNs = window.startNs == 0 ? snapshot.windowStartNs : std::min(window.startNs, snapshot.windowStartNs);
		window.endNs   = std::max(window.endNs, snapshot.windowEndNs);

		for (MetricValue& metric : snapshot.values)
		{
			if (metric.name.empty())
				continue;

			MetricKey key{
				.scope		= snapshot.scope,
				.shardIndex = snapshot.shardIndex,
				.sourceId	= snapshot.sourceId,
				.name		= std::move(metric.name),
			};

			auto [it, inserted] = window.values.try_emplace(std::move(key), AggregatedValue{ metric.value, metric.aggregation });
			if (inserted)
				continue;

			AggregatedValue& value = it->second;
			
			switch (metric.aggregation)
			{
			case eMetricAggregation::Sum:     value.value += metric.value;						 break;
			case eMetricAggregation::Maximum: value.value = std::max(value.value, metric.value); break;
			case eMetricAggregation::Latest:  value.value = metric.value;						 break;
			}
		}

		for (MetricHistogram& histogram : snapshot.histograms)
		{
			if (!histogram.IsValid())
				continue;

			MetricKey key{
				.scope = snapshot.scope,
				.shardIndex = snapshot.shardIndex,
				.sourceId = snapshot.sourceId,
				.name = histogram.Name(),
			};

			auto [it, inserted] = window.histograms.try_emplace(std::move(key), std::move(histogram));
			if (!inserted && !it->second.MergeFrom(std::move(histogram)))
				JAM_LOG_WARN("Histogram merge failed: scope={}, shard={}, source={}, metric={}", snapshot.scope, snapshot.shardIndex, snapshot.sourceId, it->first.name);
		}
	}

	void MetricsAggregator::Flush()
	{
		std::scoped_lock lock(m_mutex);

		std::vector<uint64> indices;
		
		indices.reserve(m_windows.size());
		for (const auto& index : m_windows | std::views::keys)
			indices.push_back(index);

		std::ranges::sort(indices);
		
		for (const uint64 index : indices)
		{
			if (const auto it = m_windows.find(index); it != m_windows.end())
				WriteWindow(index, it->second);
		}
		
		m_windows.clear();

		if (m_output.is_open())
			m_output.flush();
		if (m_histogramLog && m_histogramLog->file)
			std::fflush(m_histogramLog->file);
	}

	void MetricsAggregator::Shutdown()
	{
		Flush();
		std::scoped_lock lock(m_mutex);

		if (m_output.is_open())
			m_output.close();

		m_histogramLog.reset();
		
		m_config.enabled = false;
	}

	uint64 MetricsAggregator::WindowIndex(uint64 nowNs) const
	{
		const uint64 period = m_config.windowPeriodNs;
		return period != 0 ? nowNs / period : 0;
	}

	size_t MetricsAggregator::MetricKeyHash::operator()(const MetricKey& key) const
	{
		size_t hash = std::hash<std::string>{}(key.scope);
		hash ^= std::hash<uint32>{}(key.shardIndex) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
		hash ^= std::hash<uint64>{}(key.sourceId) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
		hash ^= std::hash<std::string>{}(key.name) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
		return hash;
	}

	void MetricsAggregator::FlushCompletedWindows(uint64 newestWindowIndex)
	{
		std::vector<uint64> completed;
		for (const auto& index : m_windows | std::views::keys)
		{
			// Keep one closed window as a grace period because shard/world
			// submissions can cross a boundary out of order.
			if (index + 1 < newestWindowIndex)
				completed.push_back(index);
		}

		std::ranges::sort(completed);
		
		for (const uint64 index : completed)
		{
			const auto it = m_windows.find(index);
			if (it == m_windows.end())
				continue;

			WriteWindow(index, it->second);
			m_windows.erase(it);
		}
	}

	void MetricsAggregator::WriteWindow(uint64 windowIndex, const Window& window)
	{
		if (!EnsureOutputOpen())
			return;

		const uint64 windowStartNs = windowIndex * m_config.windowPeriodNs;
		const uint64 windowEndNs   = windowStartNs + m_config.windowPeriodNs;

		std::vector<const std::pair<const MetricKey, AggregatedValue>*> rows;
		rows.reserve(window.values.size());
		for (const auto& row : window.values)
			rows.push_back(&row);

		std::ranges::sort(rows, {}, [](const auto* row)
			{
				return std::tie(row->first.scope, row->first.shardIndex, row->first.sourceId, row->first.name);
			});

		for (const auto* row : rows)
		{
			m_output << windowIndex << ',' 
					 << windowStartNs << ',' 
					 << windowEndNs << ','
					 << row->first.scope << ',' 
					 << row->first.shardIndex << ',' 
					 << row->first.sourceId << ','
					 << row->first.name << ',' 
					 << AggregationName(row->second.aggregation) << ',' 
					 << row->second.value << '\n';
		}

		WriteHistogramLog(windowIndex, window);
		m_output.flush();
		if (m_histogramLog && m_histogramLog->file)
			std::fflush(m_histogramLog->file);
	}

	void MetricsAggregator::WriteHistogramLog(const uint64 windowIndex, const Window& window)
	{
		const uint64 windowStartNs = windowIndex * m_config.windowPeriodNs;
		const uint64 windowEndNs   = windowStartNs + m_config.windowPeriodNs;

		std::vector<const std::pair<const MetricKey, MetricHistogram>*> rows;
		rows.reserve(window.histograms.size());
		for (const auto& row : window.histograms)
			rows.push_back(&row);

		std::ranges::sort(rows, {}, [](const auto* row)
			{
				return std::tie(row->first.scope, row->first.shardIndex, row->first.sourceId, row->first.name);
			});

		for (const auto* row : rows)
		{
			const MetricKey& key = row->first;
			const MetricHistogram& metric = row->second;
			const hdr_histogram* histogram = metric.m_impl->histogram;

			m_output << windowIndex << ',' 
					 << windowStartNs << ',' 
					 << windowEndNs << ','
					 << key.scope << ',' 
					 << key.shardIndex << ',' 
					 << key.sourceId << ','
					 << key.name << "_overflow,sum," 
					 << metric.m_overflowCount << '\n';

			if (!m_histogramLog || !m_histogramLog->file)
				continue;

			std::string tag = key.scope + ".shard_" + std::to_string(key.shardIndex) + ".source_" + std::to_string(key.sourceId) + '.' + key.name + ".unit_" + metric.m_config.unit;

			hdr_log_entry entry{};
			entry.start_timestamp = {
				.tv_sec = static_cast<long>(windowStartNs / 1'000'000'000ull),
				.tv_nsec = static_cast<long>(windowStartNs % 1'000'000'000ull),
			};

			const uint64 intervalNs = windowEndNs - windowStartNs;
			entry.interval = {
				.tv_sec = static_cast<long>(intervalNs / 1'000'000'000ull),
				.tv_nsec = static_cast<long>(intervalNs % 1'000'000'000ull),
			};

			hdr_timespec_from_double(&entry.max, static_cast<double>(hdr_max(histogram)));
			entry.tag = tag.data();
			entry.tag_len = tag.size();

			if (hdr_log_write_entry(&m_histogramLog->writer, m_histogramLog->file, &entry, metric.m_impl->histogram) != 0)
				JAM_LOG_WARN("Histogram log write failed: scope={}, shard={}, source={}, metric={}", key.scope, key.shardIndex, key.sourceId, key.name);
		}
	}

	bool MetricsAggregator::EnsureOutputOpen()
	{
		if (m_output.is_open())
			return true;
		if (!m_config.enabled)
			return false;
		if (m_startUnixNs == 0)
		{
			m_startMonotonicNs = NOW_NS();
			m_startUnixNs = static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		}

		std::error_code error;
		std::filesystem::create_directories(m_config.outputDirectory, error);
		if (error)
			return false;

		const std::filesystem::path path = m_config.outputDirectory / m_config.fileName;
		m_output.open(path, std::ios::out | std::ios::trunc);
		if (!m_output.is_open())
			return false;

		const std::filesystem::path histogramPath = m_config.outputDirectory / m_config.histogramFileName;
		m_histogramLog = std::make_unique<HistogramLogState>();

		if (fopen_s(&m_histogramLog->file, histogramPath.string().c_str(), "w") != 0 || !m_histogramLog->file)
		{
			m_output.close();
			m_histogramLog.reset();
			return false;
		}

		m_output << "# start_unix_ns="      << m_startUnixNs << '\n';
		m_output << "# start_monotonic_ns=" << m_startMonotonicNs << '\n';
		m_output << "# window_period_ns="   << m_config.windowPeriodNs << '\n';
		m_output << "window_index,window_start_ns,window_end_ns,scope,shard_index,source_id,metric,aggregation,value\n";

		hdr_log_writer_init(&m_histogramLog->writer);
		hdr_timespec startTimestamp{};
		hdr_gettime(&startTimestamp);

		const int headerResult = hdr_log_write_header(&m_histogramLog->writer, m_histogramLog->file, "JamNet M1 metrics", &startTimestamp);
		if (headerResult != 0)
		{
			JAM_LOG_ERROR("HDR histogram header write failed: result={}, error={}, errno={}, streamError={}",
				headerResult, hdr_strerror(headerResult), errno, std::ferror(m_histogramLog->file));

			m_output.close();
			m_histogramLog.reset();
			return false;
		}
		std::fprintf(m_histogramLog->file, "#[StartMonotonicNs: %llu]\n", static_cast<unsigned long long>(m_startMonotonicNs));
		std::fprintf(m_histogramLog->file, "#[WindowPeriodNs: %llu]\n", static_cast<unsigned long long>(m_config.windowPeriodNs));
		m_output.flush();
		std::fflush(m_histogramLog->file);
		
		return true;
	}

	std::string_view MetricsAggregator::AggregationName(eMetricAggregation aggregation)
	{
		switch (aggregation)
		{
		case eMetricAggregation::Sum:     return "sum";
		case eMetricAggregation::Maximum: return "max";
		case eMetricAggregation::Latest:  return "latest";
		}

		return "unknown";
	}
}
