#pragma once

namespace jam
{
	using spdlogRef = std::shared_ptr<spdlog::logger>;

	class Logger
	{
		DECLARE_SINGLETON(Logger)

	public:
		void			Init();
		void			Shutdown();

		spdlogRef&		GetLogger() { return m_logger; }

	private:
		spdlogRef		m_logger;
	};
}

// Basic log macros
#define JAMNET_LOG_TRACE(...) ::jam::Logger::Instance().GetLogger()->trace( __VA_ARGS__)
#define JAMNET_LOG_DEBUG(...) ::jam::Logger::Instance().GetLogger()->debug(__VA_ARGS__)
#define JAMNET_LOG_INFO(...)  ::jam::Logger::Instance().GetLogger()->info(__VA_ARGS__)
#define JAMNET_LOG_WARN(...)  ::jam::Logger::Instance().GetLogger()->warn(__VA_ARGS__)
#define JAMNET_LOG_ERROR(...) ::jam::Logger::Instance().GetLogger()->error(__VA_ARGS__)
#define JAMNET_LOG_CRITICAL(...) ::jam::Logger::Instance().GetLogger()->critical(__VA_ARGS__)

// Log macros with file location
#define JAMNET_LOG_TRACE_LOC(...) ::jam::Logger::Instance().GetLogger()->trace("[{}:{}] {}", __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define JAMNET_LOG_DEBUG_LOC(...) ::jam::Logger::Instance().GetLogger()->debug("[{}:{}] {}", __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define JAMNET_LOG_INFO_LOC(...)  ::jam::Logger::Instance().GetLogger()->info("[{}:{}] {}", __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define JAMNET_LOG_WARN_LOC(...)  ::jam::Logger::Instance().GetLogger()->warn("[{}:{}] {}", __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define JAMNET_LOG_ERROR_LOC(...) ::jam::Logger::Instance().GetLogger()->error("[{}:{}] {}", __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define JAMNET_LOG_CRITICAL_LOC(...) ::jam::Logger::Instance().GetLogger()->critical("[{}:{}] {}", __FILE__, __LINE__, fmt::format(__VA_ARGS__))

// Conditional log macros
#define JAMNET_LOG_IF(condition, level, ...) if (condition) { LOG_##level(__VA_ARGS__); }

// Convenience macros
#define JAMNET_LOG ::jam::Logger::Instance().GetLogger()
#define LOGGER_INIT() ::jam::Logger::Instance().Init()
#define LOGGER_SHUTDOWN() ::jam::Logger::Instance().Shutdown()

