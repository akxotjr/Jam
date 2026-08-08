#include "pch.h"



#include "WorldContent.h"
#include "WorldContentsLoader.h"
#include "SocialContent.h"
#include "AuthenticationContent.h"
#include "AccountStore.h"
#include "CharacterContent.h"
#include "CharacterStore.h"


int main()
{
	jam::net::RuntimeConfig runtimeConfig = {
		.geConfig = {
			.autoTune = true,
			.layoutCfg = {
				.mode			 = jam::Balance,
				.reservedThreads = 1,
				.profile		 = jam::CoreProfileServer
			}
		}
	};
	jam::net::NetRuntime runtime(runtimeConfig);


	try
	{
		const std::filesystem::path sharedDataRoot = "C://Users//akxotjr//GameWorkSpace//Jam-dev//SampleApp//M1_Shared//Data";
		auto worldContents = std::make_shared<m1::WorldContentsDatabase>(m1::WorldContentsLoader::Load((sharedDataRoot / "World" / "world_contents.json").string()));
		auto characterSessions = std::make_shared<m1::CharacterSessionStore>();
		auto characters = std::make_shared<m1::CharacterStore>();
		auto accounts = std::make_shared<m1::AccountStore>();
		const m1::WorldContentsData* initialWorldContents = worldContents->Find("Town01");
		if (!initialWorldContents)
			throw std::runtime_error("missing initial M1 world contents");
		for (jam::net::AccountId accountId = 1001; accountId <= 1016; ++accountId)
		{
			const std::string credential = std::to_string(accountId);
			accounts->Register({ .accountId = accountId, .loginId = credential, .password = credential });
			characters->Register({
				.characterId = accountId * 100 + 1,
				.accountId = accountId,
				.name = "Player" + credential,
				.actorArchetypeKey = initialWorldContents->playerActorArchetypeKey,
			});
		}

		jam::net::ServerConfig config = {};
		config.sharedDataManifestPath = (sharedDataRoot / "shared_data_manifest.json").string();
		config.socialContent = std::make_shared<m1::SocialContent>(characters, characterSessions);
		config.authenticationContent = std::make_shared<m1::AuthenticationContent>(accounts);
		config.content = std::make_shared<m1::CharacterContent>(characters, characterSessions);
		config.worldContentFactory =
			[worldContents = std::move(worldContents), characterSessions = std::move(characterSessions)](const jam::net::WorldConfig& worldConfig)
			{
				const m1::WorldContentsData* contents = worldContents->Find(worldConfig.world.instance.archetypeKey);
				if (!contents)
					throw std::runtime_error("missing M1 world contents for runtime world");

				return std::make_unique<m1::WorldContent>(*contents, characterSessions);
			};

		std::unique_ptr<jam::net::ServerNetworkManager> manager = std::make_unique<jam::net::ServerNetworkManager>(config);

		if (!manager->Start())
		{
			JAMNET_LOG_ERROR("Failed to start ServerNetworkManager");
			return -1;
		}

		std::promise<bool> bootstrapCompleted;
		auto bootstrapResult = bootstrapCompleted.get_future();
		manager->BootstrapWorldInstances([&bootstrapCompleted](bool succeeded) { bootstrapCompleted.set_value(succeeded); });
		if (!bootstrapResult.get())
		{
			JAMNET_LOG_ERROR("Failed to bootstrap configured world instances");
			manager->Stop();
			return -1;
		}

		JAMNET_LOG_INFO("TestServer started successfully");
		JAMNET_LOG_INFO("   TCP: 127.0.0.1:7777");
		JAMNET_LOG_INFO("   UDP: 127.0.0.1:8888");
		JAMNET_LOG_INFO("   key: [m]=executor metrics, [r]=metrics reset, [q]=quit");

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

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		if (manager)
		{
			manager->Stop();
			JAMNET_LOG_INFO("Server stopped gracefully");
		}

		manager.reset();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		return 0;
	}
	catch (const std::bad_weak_ptr& e)
	{
		JAMNET_LOG_DEBUG_LOC("bad_weak_ptr exception: {}", e.what());
		return -1;
	}
	catch (const std::exception& e)
	{
		JAMNET_LOG_ERROR_LOC("exception: {}", e.what());
		return -1;
	}


}
