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
		DEFAULT_ALLOCATOR_INIT();
		JAM_LOG_INIT("JamNet", "logs/jamnet.log");
		JAMNET_LOG_INFO("[NetRuntime::Init] DEFAULT_ALLOCATOR_INIT completed");
		JAMNET_LOG_INFO("JamNet default allocator: {}", DefaultAllocatorName());

		JAMNET_LOG_INFO("[NetRuntime::Init] SocketUtils::Init begin");
		SocketUtils::Init();
		JAMNET_LOG_INFO("[NetRuntime::Init] SocketUtils::Init completed");

		JAMNET_LOG_INFO("[NetRuntime::Init] Clock::Start begin");
		CLOCK.Start(m_config.clockTick);
		JAMNET_LOG_INFO("[NetRuntime::Init] Clock::Start completed");

		JAMNET_LOG_INFO("[NetRuntime::Init] MAIN_EXEC_INIT begin");
		MAIN_EXEC_INIT();
		JAMNET_LOG_INFO("[NetRuntime::Init] MAIN_EXEC_INIT completed");

		JAMNET_LOG_INFO("[NetRuntime::Init] GLOBAL_EXEC_INIT begin");
		GLOBAL_EXEC_INIT(m_config.geConfig);
		JAMNET_LOG_INFO("[NetRuntime::Init] GLOBAL_EXEC_INIT completed");

		JAMNET_LOG_INFO("[NetRuntime::Init] PHYSICS_CORE_INIT begin");
		PHYSICS_CORE_INIT();
		JAMNET_LOG_INFO("[NetRuntime::Init] PHYSICS_CORE_INIT completed");

		JAMNET_LOG_INFO("[NetRuntime::Init] GLOBAL_EXEC.Start begin");
		GLOBAL_EXEC.Start();
		JAMNET_LOG_INFO("[NetRuntime::Init] GLOBAL_EXEC.Start completed");

		JAMNET_LOG_INFO("[NetRuntime::Init] network domain bootstrap enqueue begin");
		GLOBAL_EXEC.ConveyAll(Job([]()
			{
				auto& L = CurrentShardLocalChecked();
				RegisterNetworkDomain(L);
			}));
		JAMNET_LOG_INFO("[NetRuntime::Init] network domain bootstrap enqueue completed");

		m_bInitialized = true;
		JAMNET_LOG_INFO("JamNet Runtime initialized");
	}

	void NetRuntime::Shutdown()
	{
		JAMNET_LOG_INFO("JamNet Runtime shutting down... ");

		GLOBAL_EXEC_SHUTDOWN();
		MAIN_EXEC_SHUTDOWN();
		PHYSICS_CORE_SHUTDOWN();
		SocketUtils::Clear();

		JAM_LOG_SHUTDOWN();

		m_bInitialized = false;
	}
}
