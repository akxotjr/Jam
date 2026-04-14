#include "pch.h"
#include "jamnet/core/net/JamNetRuntime.h"
#include "jamnet/core/net/SocketUtils.h"

namespace jam::net
{
	JamNetRuntime::JamNetRuntime(const RuntimeConfig& config)
		: m_config(config)
	{
		Init();
	}

	JamNetRuntime::~JamNetRuntime()
	{
		if (m_bInitialized)
			Shutdown();
	}

	void JamNetRuntime::Init()
	{
		MEMORY_MANAGER_INIT();
		JAM_LOG_INIT("JamNet", "logs/jamnet.log");
		SocketUtils::Init();
		CLOCK.Start(m_config.clockTick);
		MAIN_EXEC_INIT();
		GLOBAL_EXEC_INIT(m_config.geConfig);
		GLOBAL_EXEC.Start();

		m_bInitialized = true;
		JAMNET_LOG_INFO("JamNet Runtime initialized");
	}

	void JamNetRuntime::Shutdown()
	{
		JAMNET_LOG_INFO("JamNet Runtime shutting down... ");

		GLOBAL_EXEC_SHUTDOWN();
		SocketUtils::Clear();

		JAM_LOG_SHUTDOWN();

		m_bInitialized = false;
	}
}
