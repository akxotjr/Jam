#include "pch.h"

#include "jamnet/core/utils/TimeUnits.h"

#include "WorldContent.h"
#include "WorldContentsLoader.h"
#include "SocialContent.h"
#include "AuthenticationContent.h"
#include "AccountStore.h"
#include "CharacterContent.h"
#include "CharacterStore.h"
#include "../M1_Shared/BotScenarioPlacement.h"


int main()
{
	jam::net::RuntimeConfig runtimeConfig = {
		.geConfig = {
			.autoTune = true,
			.layoutCfg = {
				.mode			 = jam::Balance,
				.reservedThreads = 1,
				.profile		 = jam::CoreProfileServer
			},
			.metrics = {
				.enabled		 = true,
				.windowPeriodNs	 = jam::Sec(5),
				.outputDirectory = "logs/metrics",
			},
		}
	};

	jam::net::NetRuntime runtime(runtimeConfig);


	try
	{
		const std::filesystem::path sharedDataRoot = "../M1_Shared/Data";
		auto worldContents		= std::make_shared<m1::WorldContentsDatabase>(m1::WorldContentsLoader::Load((sharedDataRoot / "World" / "world_contents.json").string()));
		auto characters			= std::make_shared<m1::CharacterStore>();
		auto characterSessions	= std::make_shared<m1::CharacterSessionStore>(characters);
		auto accounts			= std::make_shared<m1::AccountStore>();

		const m1::WorldContentsData* initialWorldContents = worldContents->Find("Field");
		if (!initialWorldContents)
			throw std::runtime_error("missing initial M1 world contents");
		
		const m1::PlayerSpawnData* initialPlayerSpawn = initialWorldContents->FindDefaultPlayerSpawn();
		if (!initialPlayerSpawn)
			throw std::runtime_error("missing initial M1 player spawn");

		for (jam::net::AccountId accountId = 1001; accountId <= 1016; ++accountId)
		{
			const std::string credential = std::to_string(accountId);
			accounts->Register({ .accountId = accountId, .loginId = credential, .password = credential });
			characters->Register({
				.characterId	   = accountId * 100 + 1,
				.accountId		   = accountId,
				.name			   = "Player" + credential,
				.actorArchetypeKey = initialWorldContents->playerActorArchetypeKey,
				.worldArchetypeKey = initialWorldContents->worldArchetypeKey,
				.position		   = initialPlayerSpawn->pose.p,
			});
		}

		for (jam::net::AccountId accountId = 6000; accountId <= 9999; ++accountId)
		{
			const std::string credential = std::to_string(accountId);
			jam::px::Vec3 botSpawn = initialPlayerSpawn->pose.p;
			if (!initialWorldContents->traverseLanes.empty())
			{
				const auto placement = m1::shared::MakeBotTraversePlacement(accountId, static_cast<uint32>(initialWorldContents->traverseLanes.size()));
				const m1::TraverseLaneData& lane = initialWorldContents->traverseLanes[placement.laneIndex];
				botSpawn = lane.start + lane.direction * (lane.length * placement.phase);
			}
			accounts->Register({ .accountId = accountId, .loginId = credential, .password = credential });
			characters->Register({
				.characterId	   = accountId * 100,
				.accountId		   = accountId,
				.name			   = "Bot" + credential,
				.actorArchetypeKey = initialWorldContents->playerActorArchetypeKey,
				.worldArchetypeKey = initialWorldContents->worldArchetypeKey,
				.position		   = botSpawn,
			});
		}

		jam::net::ServerConfig config = {};

		config.maxConnections				 = 4000;
		config.sharedDataManifestPath		 = (sharedDataRoot / "shared_data_manifest.json").string();
		config.socialContent				 = std::make_shared<m1::SocialContent>(characters, characterSessions);
		config.authenticationContent		 = std::make_shared<m1::AuthenticationContent>(accounts);
		config.content						 = std::make_shared<m1::CharacterContent>(characters, characterSessions);
		config.enterWorldDestinationResolver = [characterSessions](jam::net::AccountId accountId, jam::net::UserId userId)
			-> std::optional<jam::net::WorldArchetypeKey>
			{
				const auto character = characterSessions->FindSelectedCharacter(userId);
				if (!character || character->accountId != accountId)
					return std::nullopt;

				return character->worldArchetypeKey;
			};
		config.worldContentFactory = [worldContents = std::move(worldContents), characterSessions = std::move(characterSessions)](const jam::net::WorldConfig& worldConfig)
			{
				const m1::WorldContentsData* contents = worldContents->Find(worldConfig.world.instance.archetypeKey);
				const m1::WorldInstanceContentsData* instance = worldContents->Find(worldConfig.world.instance.instanceId);
				if (!contents || !instance || instance->worldArchetypeKey != contents->worldArchetypeKey)
					throw std::runtime_error("missing M1 world contents for runtime world");

				return std::make_unique<m1::WorldContent>(*contents, *instance, characterSessions);
			};

		std::unique_ptr<jam::net::ServerNetworkManager> manager = std::make_unique<jam::net::ServerNetworkManager>(config);

		if (!manager->Start())
		{
			JAM_LOG_ERROR("Failed to start ServerNetworkManager");
			return -1;
		}

		std::promise<bool> bootstrapCompleted;
		auto bootstrapResult = bootstrapCompleted.get_future();
		manager->BootstrapWorldInstances([&bootstrapCompleted](bool succeeded) { bootstrapCompleted.set_value(succeeded); });
		if (!bootstrapResult.get())
		{
			JAM_LOG_ERROR("Failed to bootstrap configured world instances");
			manager->Stop();
			return -1;
		}

		JAM_LOG_INFO("Server started successfully");
		JAM_LOG_INFO("[q]=quit");

		while (true)
		{
			if (_kbhit())
			{
				char ch = static_cast<char>(_getch());
				if (ch == 'q' || ch == 'Q')
				{
					JAM_LOG_INFO("Shutting down server...");
					break;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		if (manager)
		{
			manager->Stop();
			JAM_LOG_INFO("Server stopped gracefully");
		}

		manager.reset();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		return 0;
	}
	catch (const std::bad_weak_ptr& e)
	{
		JAM_LOG_DEBUG_LOC("bad_weak_ptr exception: {}", e.what());
		return -1;
	}
	catch (const std::exception& e)
	{
		JAM_LOG_ERROR_LOC("exception: {}", e.what());
		return -1;
	}


}
