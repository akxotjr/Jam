#include "pch.h"
#include "jamnet/core/net/NetRuntime.h"

#include "jamnet/core/executor/MainExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/memory/DefaultAllocator.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/utils/Clock.h"
#include "jampx/PhysicsCore.h"

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
		JAM_LOG_INIT(m_config.logFilePath);
		DEFAULT_ALLOCATOR_INIT();
		SocketUtils::Init();
		CLOCK.Start(m_config.clockTick);
		MAIN_EXEC_INIT();
		GLOBAL_EXEC_INIT(m_config.geConfig);
		PHYSICS_CORE_INIT();
		GLOBAL_EXEC.Start();
		GLOBAL_EXEC.ConveyAll(Job([]()
			{
				auto& L = CurrentShardLocalChecked();
				RegisterNetworkDomain(L);
			}));

		m_bInitialized = true;
		JAM_LOG_INFO("JamNet Runtime initialized");
	}

	void NetRuntime::Shutdown()
	{
		JAM_LOG_INFO("JamNet Runtime shutting down... ");

		GLOBAL_EXEC_SHUTDOWN();
		MAIN_EXEC_SHUTDOWN();
		PHYSICS_CORE_SHUTDOWN();
		SocketUtils::Clear();

		JAM_LOG_SHUTDOWN();

		m_bInitialized = false;
	}
}
