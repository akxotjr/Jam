#pragma once

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace jam
{
	using spdlogRef = std::shared_ptr<spdlog::logger>;

	namespace detail
	{
		template <typename... Args>
		inline std::string FormatLogMessage(std::string_view format, Args&&... args)
		{
			try
			{
				return fmt::vformat(fmt::string_view(format.data(), format.size()), fmt::make_format_args(args...));
			}
			catch (...)
			{
				return std::string(format);
			}
		}
	}

	class Logger
	{
	public:
		static Logger& Instance()
		{
			static Logger instance;
			return instance;
		}

		void Init(std::string_view name = "Jam", std::string_view filePath = "logs/jam.log")
		{
			std::lock_guard lock(m_mutex);
			if (m_logger)
				return;

			const std::string loggerName(name);
			if (auto existing = spdlog::get(loggerName))
			{
				m_logger = std::move(existing);
				return;
			}

			spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [T%t] [%^%l%$] %v");

			std::vector<spdlog::sink_ptr> sinks;
			sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

			if (!filePath.empty())
			{
				try
				{
					sinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::string(filePath), true));
				}
				catch (...)
				{
					WriteFallback(spdlog::level::warn, "Failed to create log file sink; console logging remains active");
				}
			}

			m_logger = std::make_shared<spdlog::logger>(loggerName, sinks.begin(), sinks.end());
			m_logger->set_level(spdlog::level::trace);
			spdlog::register_logger(m_logger);
		}

		void Shutdown()
		{
			spdlogRef logger;
			{
				std::lock_guard lock(m_mutex);
				logger = std::move(m_logger);
			}

			if (logger)
			{
				logger->flush();
				spdlog::drop(logger->name());
			}

			spdlog::shutdown();
		}

		spdlogRef GetLogger()
		{
			std::lock_guard lock(m_mutex);
			return m_logger;
		}

		template <typename... Args>
		void Log(spdlog::level::level_enum level, std::string_view format, Args&&... args)
		{
			const std::string message = detail::FormatLogMessage(format, args...);
			if (auto logger = GetLogger())
			{
				logger->log(level, "{}", message);
				return;
			}

			WriteFallback(level, message);
		}

	private:
		Logger() = default;
		~Logger() = default;
		Logger(const Logger&) = delete;
		Logger& operator=(const Logger&) = delete;

		static const char* LevelName(spdlog::level::level_enum level)
		{
			switch (level)
			{
			case spdlog::level::trace:		return "TRACE";
			case spdlog::level::debug:		return "DEBUG";
			case spdlog::level::info:		return "INFO";
			case spdlog::level::warn:		return "WARN";
			case spdlog::level::err:		return "ERROR";
			case spdlog::level::critical:	return "FATAL";
			default:						return "LOG";
			}
		}

		static void WriteFallback(spdlog::level::level_enum level, std::string_view message) noexcept
		{
			std::fprintf(stderr, "[JAM][%s] %.*s\n", LevelName(level), static_cast<int>(message.size()), message.data());
		}

		std::mutex m_mutex;
		spdlogRef  m_logger;
	};

	template <typename... Args>
	inline void LogTrace(std::string_view format, Args&&... args)
	{
		Logger::Instance().Log(spdlog::level::trace, format, args...);
	}

	template <typename... Args>
	inline void LogDebug(std::string_view format, Args&&... args)
	{
		Logger::Instance().Log(spdlog::level::debug, format, args...);
	}

	template <typename... Args>
	inline void LogInfo(std::string_view format, Args&&... args)
	{
		Logger::Instance().Log(spdlog::level::info, format, args...);
	}

	template <typename... Args>
	inline void LogWarn(std::string_view format, Args&&... args)
	{
		Logger::Instance().Log(spdlog::level::warn, format, args...);
	}

	template <typename... Args>
	inline void LogError(std::string_view format, Args&&... args)
	{
		Logger::Instance().Log(spdlog::level::err, format, args...);
	}

	template <typename... Args>
	inline void LogCritical(std::string_view format, Args&&... args)
	{
		Logger::Instance().Log(spdlog::level::critical, format, args...);
	}

	template <typename... Args>
	inline void LogLocation(spdlog::level::level_enum level, const char* file, int line, std::string_view format, Args&&... args)
	{
		Logger::Instance().Log(level, "[{}:{}] {}", file, line, detail::FormatLogMessage(format, args...));
	}
}

#define JAM_LOG_TRACE(...)			::jam::LogTrace(__VA_ARGS__)
#define JAM_LOG_DEBUG(...)			::jam::LogDebug(__VA_ARGS__)
#define JAM_LOG_INFO(...)			::jam::LogInfo(__VA_ARGS__)
#define JAM_LOG_WARN(...)			::jam::LogWarn(__VA_ARGS__)
#define JAM_LOG_ERROR(...)			::jam::LogError(__VA_ARGS__)
#define JAM_LOG_CRITICAL(...)		::jam::LogCritical(__VA_ARGS__)
#define JAM_LOG_FATAL(...)			::jam::LogCritical(__VA_ARGS__)

#define JAM_LOG_TRACE_LOC(...)		::jam::LogLocation(::spdlog::level::trace, __FILE__, __LINE__, __VA_ARGS__)
#define JAM_LOG_DEBUG_LOC(...)		::jam::LogLocation(::spdlog::level::debug, __FILE__, __LINE__, __VA_ARGS__)
#define JAM_LOG_INFO_LOC(...)		::jam::LogLocation(::spdlog::level::info, __FILE__, __LINE__, __VA_ARGS__)
#define JAM_LOG_WARN_LOC(...)		::jam::LogLocation(::spdlog::level::warn, __FILE__, __LINE__, __VA_ARGS__)
#define JAM_LOG_ERROR_LOC(...)		::jam::LogLocation(::spdlog::level::err, __FILE__, __LINE__, __VA_ARGS__)
#define JAM_LOG_CRITICAL_LOC(...)	::jam::LogLocation(::spdlog::level::critical, __FILE__, __LINE__, __VA_ARGS__)
#define JAM_LOG_FATAL_LOC(...)		::jam::LogLocation(::spdlog::level::critical, __FILE__, __LINE__, __VA_ARGS__)

#define JAM_LOG_IF(condition, level, ...) do { if (condition) { JAM_LOG_##level(__VA_ARGS__); } } while (false)

#define JAM_LOG				::jam::Logger::Instance().GetLogger()
#define JAM_LOG_INIT(...)	::jam::Logger::Instance().Init(__VA_ARGS__)
#define JAM_LOG_SHUTDOWN()	::jam::Logger::Instance().Shutdown()

#define JAMNET_LOG_TRACE(...)		JAM_LOG_TRACE(__VA_ARGS__)
#define JAMNET_LOG_DEBUG(...)		JAM_LOG_DEBUG(__VA_ARGS__)
#define JAMNET_LOG_INFO(...)		JAM_LOG_INFO(__VA_ARGS__)
#define JAMNET_LOG_WARN(...)		JAM_LOG_WARN(__VA_ARGS__)
#define JAMNET_LOG_ERROR(...)		JAM_LOG_ERROR(__VA_ARGS__)
#define JAMNET_LOG_CRITICAL(...)	JAM_LOG_CRITICAL(__VA_ARGS__)

#define JAMNET_LOG_TRACE_LOC(...)	JAM_LOG_TRACE_LOC(__VA_ARGS__)
#define JAMNET_LOG_DEBUG_LOC(...)	JAM_LOG_DEBUG_LOC(__VA_ARGS__)
#define JAMNET_LOG_INFO_LOC(...)	JAM_LOG_INFO_LOC(__VA_ARGS__)
#define JAMNET_LOG_WARN_LOC(...)	JAM_LOG_WARN_LOC(__VA_ARGS__)
#define JAMNET_LOG_ERROR_LOC(...)	JAM_LOG_ERROR_LOC(__VA_ARGS__)
#define JAMNET_LOG_CRITICAL_LOC(...) JAM_LOG_CRITICAL_LOC(__VA_ARGS__)

#define JAMNET_LOG_IF(condition, level, ...) JAM_LOG_IF(condition, level, __VA_ARGS__)
#define JAMNET_LOG JAM_LOG
#define LOGGER_INIT() JAM_LOG_INIT("JamNet", "logs/jamnet.log")
#define LOGGER_SHUTDOWN() JAM_LOG_SHUTDOWN()
