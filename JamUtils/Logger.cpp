#include "pch.h"
#include "Logger.h"


namespace jam::utils
{
	void Logger::Init()
	{
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [T%t] [%^%l%$] %v");

		auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/server.log", true);

		xvector<spdlog::sink_ptr> sinks{ consoleSink, fileSink };

		_logger = std::make_shared<spdlog::logger>("JamNet", sinks.begin(), sinks.end());
		_logger->set_level(spdlog::level::trace);
	}

	void Logger::Shutdown()
	{
		ISingletonLayer::Shutdown();
	}
}