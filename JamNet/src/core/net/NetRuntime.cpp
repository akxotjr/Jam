#include "pch.h"
#include "jamnet/core/net/NetRuntime.h"

#include "jamnet/core/executor/MainExecutor.h"
#include "jamnet/core/memory/DefaultAllocator.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/utils/Clock.h"

namespace jam::net
{
	NetRuntime::NetRuntime(const RuntimeConfig& config)
		: m_config(config)
	{
		Init();
	}

	NetRuntime::~NetRuntime()
	{
		if (m_bInitialized)
			Shutdown();
	}

	void NetRuntime::Init()
	{
		DEFAULT_ALLOCATOR_INIT();
		JAM_LOG_INIT("JamNet", "logs/jamnet.log");
		JAMNET_LOG_INFO("JamNet default allocator: {}", DefaultAllocatorName());
		SocketUtils::Init();
		CLOCK.Start(m_config.clockTick);
		MAIN_EXEC_INIT();
		GLOBAL_EXEC_INIT(m_config.geConfig);
		GLOBAL_EXEC.Start();

		m_bInitialized = true;
		JAMNET_LOG_INFO("JamNet Runtime initialized");
	}

	void NetRuntime::Shutdown()
	{
		JAMNET_LOG_INFO("JamNet Runtime shutting down... ");

		GLOBAL_EXEC_SHUTDOWN();
		SocketUtils::Clear();

		JAM_LOG_SHUTDOWN();

		m_bInitialized = false;
	}
}
