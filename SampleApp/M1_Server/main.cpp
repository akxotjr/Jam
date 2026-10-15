#include "pch.h"

#include "jamnet/core/utils/TimeUnits.h"

#include "WorldContent.h"
#include "WorldContentsLoader.h"
#include "SocialContent.h"
#include "Authenticator.h"
#include "AccountStore.h"
#include "CharacterContent.h"
#include "CharacterStore.h"
#include "../M1_Shared/BotScenarioPlacement.h"
#include <charconv>
#include <fstream>


int main(int argc, char* argv[])
{
	std::string bindIp = "192.168.50.1";
	uint16 tcpPort = 7777;
	uint16 udpPort = 8888;
	std::filesystem::path sharedDataRoot = "../M1_Shared/Data";
	std::filesystem::path outputDirectory;
	std::filesystem::path readyFile;
	std::filesystem::path stopFile;

	auto printUsage = []()
	{
		std::cout << "Usage: M1_Server.exe [options]\n"
			"  --bind_ip <address>     Bind address (default: 192.168.50.1)\n"
			"  --tcp_port <port>       TCP port (default: 7777)\n"
			"  --udp_port <port>       UDP port (default: 8888)\n"
			"  --data_root <path>      Shared data root (default: ../M1_Shared/Data)\n"
			"  --output_dir <path>     Log and metrics directory\n"
			"  --ready_file <path>     Created after world bootstrap; must not exist\n"
			"  --stop_file <path>      Create this file to request graceful shutdown\n";
	};
	
	for (int i = 1; i < argc; i += 2)
	{
		const std::string_view option(argv[i]);
	
		if (option == "--help") { printUsage(); return 0; }
		if (i + 1 >= argc)		{ printUsage(); return 1; }

		const std::string_view value(argv[i + 1]);
		bool valid = !value.empty() && !value.starts_with("--");
		
		if (valid && option == "--bind_ip")			bindIp			= value;
		else if (valid && option == "--data_root")	sharedDataRoot	= argv[i + 1];
		else if (valid && option == "--output_dir") outputDirectory = argv[i + 1];
		else if (valid && option == "--ready_file") readyFile		= argv[i + 1];
		else if (valid && option == "--stop_file")	stopFile		= argv[i + 1];
		else if (valid && (option == "--tcp_port" || option == "--udp_port"))
		{
			uint32 port = 0;
			const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
			valid = error == std::errc{} && end == value.data() + value.size() && port > 0 && port <= 65535;
			
			if (valid && option == "--tcp_port") 
				tcpPort = static_cast<uint16>(port);
			else if (valid) 
				udpPort = static_cast<uint16>(port);
		}
		else valid = false;
		
		if (!valid)
		{
			std::cerr << "Invalid option or value: " << option << ' ' << value << '\n'; printUsage(); return 1;
		}
	}

	try
	{
		if ((!readyFile.empty() && std::filesystem::exists(readyFile)) || (!stopFile.empty() && std::filesystem::exists(stopFile)))
			throw std::runtime_error("ready/stop file already exists; use fresh per-run paths");
		if (!readyFile.empty() && !stopFile.empty() &&
			std::filesystem::absolute(readyFile).lexically_normal() == std::filesystem::absolute(stopFile).lexically_normal())
			throw std::runtime_error("ready and stop files must have different paths");
		if (!outputDirectory.empty()) std::filesystem::create_directories(outputDirectory);
	}
	catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }

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
				.windowPeriod	 = jam::Sec(5),
				.outputDirectory = "logs/metrics",
			},
		}
	};

	if (!outputDirectory.empty())
	{
		runtimeConfig.logFilePath = (outputDirectory / (jam::detail::TimestampedStem() + ".log")).string();
		runtimeConfig.geConfig.metrics.outputDirectory = outputDirectory;
	}

	jam::net::NetRuntime runtime(runtimeConfig);


	try
	{
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

		config.tcpAddress = { bindIp, tcpPort };
		config.udpAddress = { bindIp, udpPort };

		config.maxConnections				 = 4000;
		config.sharedDataManifestPath		 = (sharedDataRoot / "shared_data_manifest.json").string();
		config.socialContent				 = std::make_shared<m1::SocialContent>(characters, characterSessions);
		config.authenticator				 = std::make_shared<m1::Authenticator>(accounts);
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
		if (!readyFile.empty())
		{
			try
			{
				if (readyFile.has_parent_path()) std::filesystem::create_directories(readyFile.parent_path());
				std::ofstream ready(readyFile);
				ready << "ready\n";
				ready.close();
				if (!ready) throw std::runtime_error("failed to write ready file");
			}
			catch (...) { manager->Stop(); throw; }
		}
		DWORD consoleMode = 0;
		const bool interactive = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &consoleMode) != 0;
		bool controlFailed = false;
		JAM_LOG_INFO("[q]=quit");

		while (true)
		{
			std::error_code controlError;
			const bool stopRequested = !stopFile.empty() && std::filesystem::exists(stopFile, controlError);
			if (controlError)
			{
				JAM_LOG_ERROR("Failed to inspect stop file: {}", controlError.message());
				controlFailed = true;
				break;
			}
			if (stopRequested)
			{
				JAM_LOG_INFO("Shutting down server: stop file detected");
				break;
			}
			if (interactive && _kbhit())
			{
				char ch = static_cast<char>(_getch());
				if (ch == 'q' || ch == 'Q')
				{
					JAM_LOG_INFO("Shutting down server...");
					break;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		if (manager)
		{
			manager->Stop();
			JAM_LOG_INFO("Server stopped gracefully");
		}

		manager.reset();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		return controlFailed ? 1 : 0;
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
