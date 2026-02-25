#include "pch.h"
#include "jamnet/core/utils/Logger.h"


namespace jam
{
	void Logger::Init()
	{
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [T%t] [%^%l%$] %v");

		auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/jamnet.log", true);

		std::vector<spdlog::sink_ptr> sinks{ consoleSink, fileSink };

		_logger = std::make_shared<spdlog::logger>("JamNet", sinks.begin(), sinks.end());
		_logger->set_level(spdlog::level::trace);

		spdlog::register_logger(_logger);
	}

	void Logger::Shutdown()
	{
		if (_logger)
		{
			_logger->flush();
			spdlog::drop(_logger->name());
			_logger.reset();
		}

		spdlog::shutdown();
	}
}