#pragma once
#include "ISingletonLayer.h"

namespace jam::utils
{
	using spdlogRef = std::shared_ptr<spdlog::logger>;

	class Logger : public ISingletonLayer<Logger>
	{
		friend class jam::ISingletonLayer<Logger>;

	public:
		void			Init() override;
		spdlogRef&		GetLogger() { return _logger; }

	private:
		spdlogRef		_logger;
	};
}

#define LOG_TRACE(...) Logger::Instance()->GetLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) Logger::Instance()->GetLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...)  Logger::Instance()->GetLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)  Logger::Instance()->GetLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) Logger::Instance()->GetLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) Logger::Instance()->GetLogger()->critical(__VA_ARGS__)
#define LOG_OFF(...) Logger::Instance()->GetLogger()->off(__VA_ARGS__)

