#include "pch.h"
#include <conio.h>
#include <filesystem>


#include "jampx/PhysicsCore.h"
#include "jampx/PhysicsFacade.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"

#include "ExecutorMetricsReporter.h"

using namespace std;

using namespace jam::net;



int main()
{
	RuntimeConfig config{};
	config.geConfig = {
		.autoTune = true,
		.layoutCfg = {
			.mode = jam::Balance,
			.reservedThreads = 1,
			.profile = jam::CoreProfileServer,
		}
	};
	NetRuntime runtime(config);

	try
	{

		ServerConfig serverCfg{};
		serverCfg.physicsFactory = []() { return std::make_unique<jam::px::PhysicsFacade>(); };
		serverCfg.worldAssetPath = "C://Users//akxotjr//GameWorkSpace//Jam-dev//TestApp//Contents//world_templates.json";

		auto serverManager = std::make_unique<ServerNetworkManager>(serverCfg);

		if (!serverManager->Start())
		{
			JAMNET_LOG_ERROR("Failed to start server");
			return -1;
		}

		JAMNET_LOG_INFO("TestServer started successfully");
		JAMNET_LOG_INFO("   TCP: 127.0.0.1:7777");
		JAMNET_LOG_INFO("   UDP: 127.0.0.1:8888");
		JAMNET_LOG_INFO("   key: [m]=executor metrics, [r]=metrics reset, [q]=quit");
		
		PHYSICS_CORE_INIT();
		JAMNET_LOG_INFO("PhysicsCore initialized successfully");
		
		PHYSICS_PREFAB_REGISTRY.Init("C://Users//akxotjr//GameWorkSpace//Jam-dev//TestApp//Contents//test_asset.json");
		JAMNET_LOG_INFO("PhysicsPrefabRegistry initialized successfully");


		std::this_thread::sleep_for(500ms);

		testserver::ExecutorCsvReporter executorReporter{};
		executorReporter.WriteSample(testserver::CaptureExecutorMetricsSample());
		constexpr auto executorMetricsSamplePeriod = std::chrono::seconds(5);
		auto nextExecutorMetricsSample = std::chrono::steady_clock::now() + executorMetricsSamplePeriod;

		while (true)
		{
			const auto now = std::chrono::steady_clock::now();
			if (now >= nextExecutorMetricsSample)
			{
				executorReporter.WriteSample(testserver::CaptureExecutorMetricsSample());
				do
				{
					nextExecutorMetricsSample += executorMetricsSamplePeriod;
				} while (now >= nextExecutorMetricsSample);
			}

			if (_kbhit())
			{
				char ch = static_cast<char>(_getch());
				if (ch == 'q' || ch == 'Q')
				{
					JAMNET_LOG_INFO("Shutting down server...");
					break;
				}
			}

			std::this_thread::sleep_for(10ms);
		}


		JAMNET_LOG_INFO("Shutting down server...");
		executorReporter.WriteSample(testserver::CaptureExecutorMetricsSample());
		executorReporter.WriteSummary();

		if (serverManager)
		{
			serverManager->Stop();
			JAMNET_LOG_INFO("Server stopped gracefully");
		}

		serverManager.reset();
		std::this_thread::sleep_for(200ms);

		return 0;
	}
	catch (const std::bad_weak_ptr& e)
	{
		JAMNET_LOG_ERROR_LOC("bad_weak_ptr exception: {}", e.what());
		return -1;
	}
	catch (const std::exception& e)
	{
		JAMNET_LOG_ERROR_LOC("Exception: {}", e.what());
		return -1;
	}
}
