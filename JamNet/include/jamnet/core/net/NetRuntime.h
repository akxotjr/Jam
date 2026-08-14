#pragma once
#include "jamnet/core/executor/GlobalExecutor.h"

#include <string>


namespace jam::net
{
	struct RuntimeConfig
	{
		uint32					clockTick = 30;
		GlobalExecutorConfig	geConfig  = {};
		std::string				logFilePath = "logs/jamnet.log";
	};

	class NetRuntime
	{
	public:
		explicit NetRuntime(const RuntimeConfig& config = {});
		~NetRuntime();

		NetRuntime(const NetRuntime&) = delete;
		NetRuntime& operator=(const NetRuntime&) = delete;

		bool			IsInitialized() const noexcept { return m_bInitialized; }

	private:
		void			Init();
		void			Shutdown();

	private:
		RuntimeConfig	m_config;
		bool			m_bInitialized = false;
	};
}
