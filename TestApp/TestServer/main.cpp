#include "pch.h"
#include <conio.h>

#include "jamnet/runtime/ServerNetworkManager.h"
#include "jampx/PhysicsCore.h"
#include "jampx/PhysicsFacade.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"

using namespace jam::net;



int main()
{
	RuntimeConfig config{};
	config.geConfig = {
		.autoTune = true,
		.layoutCfg = {
			.mode = jam::BALANCE,
			.reserved_threads = 1,
			.profile = jam::CORE_PROFILE_SERVER,
		}
	};
	JamNetRuntime runtime(config);

	try
	{
		ServerConfig serverCfg{};
		serverCfg.physicsFactory = []() { return std::make_unique<jam::px::PhysicsFacade>(); };
		serverCfg.levelPath = "C://Users//akxotjr//GameWorkSpace//Jam//TestApp//TestClient//Contents//test_level.json";
		auto serverManager = std::make_unique<ServerNetworkManager>(serverCfg);

		if (!serverManager->Start())
		{
			JAMNET_LOG_ERROR("Failed to start server");
			return -1;
		}

		JAMNET_LOG_INFO("TestServer started successfully");
		JAMNET_LOG_INFO("   TCP: 127.0.0.1:7777");
		JAMNET_LOG_INFO("   UDP: 127.0.0.1:8888");
		
		PHYSICS_CORE_INIT();
		JAMNET_LOG_INFO("PhysicsCore initialized successfully");
		
		PHYSICS_PREFAB_REGISTRY.Init("C://Users//akxotjr//GameWorkSpace//Jam//TestApp//TestClient//Contents//test_prefab.json");
		JAMNET_LOG_INFO("PhysicsPrefabRegistry initialized successfully");


		std::this_thread::sleep_for(500ms);

		while (true)
		{
			if (_kbhit())
			{
				char ch = static_cast<char>(_getch());
				if (ch == 'q' || ch == 'Q')
				{
					JAMNET_LOG_INFO("Shutting down server...");
					break;
				}
			}
		}


		JAMNET_LOG_INFO("Shutting down server...");

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