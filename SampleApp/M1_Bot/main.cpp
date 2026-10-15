#include "pch.h"

#include "BotRunner.h"

#include <charconv>
#include <filesystem>


namespace
{
	bool ParseUInt32(const char* text, uint32& out)
	{
		if (!text || *text == '\0')
			return false;

		const char* end = text + std::char_traits<char>::length(text);
		const auto [ptr, error] = std::from_chars(text, end, out);
		return error == std::errc{} && ptr == end;
	}

	void PrintUsage()
	{
		std::cout << "Usage: M1_Bot.exe <profile> [options]\n";
		std::cout << "  profile: baseline|target|peak|stress|aoi-hotspot|portal-burst\n";
		std::cout << "  --ip <address>          Server IP (default: 127.0.0.1)\n";
		std::cout << "  --tcp_port <port>       TCP port, 1-65535 (default: 7777)\n";
		std::cout << "  --udp_port <port>       UDP port, 1-65535 (default: 8888)\n";
		std::cout << "  --ccu <count>           Bots, 1-4000 (default: baseline 300, others 1000)\n";
		std::cout << "  --warm_up <minutes>     Warm-up duration, 0 or more (default: 3)\n";
		std::cout << "  --measurement <minutes> Measurement duration, 1 or more (default: 10)\n";
		std::cout << "  --data_root <path>      Shared data root (default: ../M1_Shared/Data)\n";
		std::cout << "  --output_dir <path>     Log directory\n";
		std::cout << "  --seed <number>         Random seed (default: 1)\n";
		std::cout << "  --connect_per_second <count> Connection starts per second (default: 50)\n";
		std::cout << "  --stats_interval <seconds> Automatic stats period, 0 disables (default: 5)\n";
		std::cout << "  --stop_file <path>      Create this file to abort gracefully\n";
	}

	enum class eValidationPhase
	{
		RampUp,
		WarmUp,
		Measurement,
		CoolDown,
	};

	std::string_view PhaseName(eValidationPhase phase)
	{
		switch (phase)
		{
		case eValidationPhase::RampUp:     return "ramp-up";
		case eValidationPhase::WarmUp:     return "warm-up";
		case eValidationPhase::Measurement:return "measurement";
		case eValidationPhase::CoolDown:   return "cool-down";
		default:                           return "unknown";
		}
	}

	uint64 UnixNowNs()
	{
		return static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	}

	void LogPhase(const eValidationPhase phase, const std::string_view event)
	{
		JAM_LOG_INFO("[Phase] {} {} unix_ns={}", PhaseName(phase), event, UnixNowNs());
	}

	std::optional<eBotProfile> ParseProfile(std::string_view name)
	{
		if (name == "baseline") return eBotProfile::Baseline;
		if (name == "target") return eBotProfile::Target;
		if (name == "peak") return eBotProfile::Peak;
		if (name == "stress") return eBotProfile::Stress;
		if (name == "aoi-hotspot") return eBotProfile::AoiHotspot;
		if (name == "portal-burst") return eBotProfile::PortalBurst;
		return std::nullopt;
	}

	void PrintStats(const BotRunnerStats& stats, eValidationPhase phase,
		std::chrono::steady_clock::duration elapsed, std::chrono::steady_clock::duration phaseElapsed)
	{
		JAM_LOG_INFO(
			"[M1BotStats] phase={}, elapsedSec={}, phaseElapsedSec={}, "
			"started={}/{}, connectRejected={}, connecting={}, ready={}, inWorld={}, running={}, failed={}, "
			"chatTx={}, chatRx={}, chatRejected={}, portal={}, portalTimeout={}, unexpectedWorld={}",
			PhaseName(phase),
			std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(),
			std::chrono::duration_cast<std::chrono::seconds>(phaseElapsed).count(),
			stats.connectStarted,
			stats.total,
			stats.connectRejected,
			stats.connecting,
			stats.ready,
			stats.inWorld,
			stats.running,
			stats.failed,
			stats.chatSent,
			stats.chatReceived,
			stats.chatRejected,
			stats.portalTransitions,
			stats.portalTimeouts,
			stats.unexpectedWorldTransitions);
	}
}


int main(int argc, char* argv[])
{
	BotRunnerConfig botConfig{};
	uint32 warmUpMinutes = 3;
	uint32 measurementMinutes = 10;
	uint32 statsIntervalSeconds = 5;
	std::filesystem::path outputDirectory;
	std::filesystem::path stopFile;
	if (argc == 2 && std::string_view(argv[1]) == "--help") { PrintUsage(); return 0; }
	const auto profile = argc >= 2 ? ParseProfile(argv[1]) : std::nullopt;
	if (!profile)
	{
		PrintUsage();
		return -1;
	}
	botConfig.profile = *profile;
	botConfig.botCount = *profile == eBotProfile::Baseline ? 300 : 1000;
	for (int i = 2; i < argc; i += 2)
	{
		const std::string_view option(argv[i]);
		if (i + 1 >= argc)
		{
			std::cerr << "Missing value for option: " << option << '\n';
			PrintUsage();
			return -1;
		}

		const char* value = argv[i + 1];
		bool valid = false;
		if (option == "--ip" || option == "--data_root" || option == "--output_dir" || option == "--stop_file")
		{
			valid = *value != '\0' && !std::string_view(value).starts_with("--");
			if (valid && option == "--ip")
				botConfig.serverIp = value;
			else if (valid && option == "--data_root")
				botConfig.sharedDataManifestPath = (std::filesystem::path(value) / "shared_data_manifest.json").string();
			else if (valid && option == "--output_dir") outputDirectory = value;
			else if (valid) stopFile = value;
		}
		else
		{
			uint32 number = 0;
			if (ParseUInt32(value, number))
			{
				if (option == "--tcp_port" || option == "--udp_port")
				{
					valid = number > 0 && number <= 65535;
					if (valid && option == "--tcp_port")
						botConfig.tcpPort = static_cast<uint16>(number);
					else if (valid)
						botConfig.udpPort = static_cast<uint16>(number);
				}
				else if (option == "--ccu")
				{
					valid = number > 0 && number <= 4000;
					if (valid)
						botConfig.botCount = number;
				}
				else if (option == "--warm_up")
				{
					valid = true;
					warmUpMinutes = number;
				}
				else if (option == "--measurement")
				{
					valid = number > 0;
					if (valid)
						measurementMinutes = number;
				}
				else if (option == "--seed") { valid = true; botConfig.randomSeed = number; }
				else if (option == "--stats_interval") { valid = true; statsIntervalSeconds = number; }
				else if (option == "--connect_per_second")
				{
					valid = number > 0 && number <= 4000;
					if (valid) botConfig.connectPerSecond = number;
				}
			}
		}
		if (!valid)
		{
			std::cerr << "Invalid option or value: " << option << ' ' << value << '\n';
			PrintUsage();
			return -1;
		}
	}
	try
	{
		if (!stopFile.empty() && std::filesystem::exists(stopFile))
			throw std::runtime_error("stop file already exists; use a fresh per-run path");
		if (!outputDirectory.empty()) std::filesystem::create_directories(outputDirectory);
	}
	catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }

	jam::net::RuntimeConfig runtimeConfig{
		.geConfig = {
			.autoTune = true,
			.layoutCfg = {
				.mode = jam::Balance,
				.reservedThreads = 1,
				.profile = jam::CoreProfileMultipleClient,
			},
		},
	};
	if (!outputDirectory.empty())
		runtimeConfig.logFilePath = (outputDirectory / (jam::detail::TimestampedStem() + ".log")).string();
	jam::net::NetRuntime runtime(runtimeConfig);
	if (!runtime.IsInitialized())
	{
		JAM_LOG_ERROR("Failed to initialize NetRuntime");
		return -1;
	}

	BotRunner runner;
	if (!runner.Initialize(botConfig))
	{
		JAM_LOG_ERROR("Failed to initialize BotRunner");
		return -1;
	}

	runner.Start();

	constexpr auto kRampSettleTimeout = std::chrono::minutes(5);

	const auto warmUpDuration		 = std::chrono::minutes(warmUpMinutes);
	const auto measurementDuration	 = std::chrono::minutes(measurementMinutes);
	const auto runStartedAt			 = std::chrono::steady_clock::now();
	const auto scheduledRampDuration = std::chrono::seconds((botConfig.botCount + botConfig.connectPerSecond - 1) / botConfig.connectPerSecond);
	const auto rampDeadline			 = runStartedAt + scheduledRampDuration + kRampSettleTimeout;

	eValidationPhase phase = eValidationPhase::RampUp;
	
	auto phaseStartedAt = runStartedAt;
	auto phaseDeadline = rampDeadline;
	
	LogPhase(phase, "start");

	JAM_LOG_INFO("M1_Bot started: profile={}, bots={}, connectPerSecond={}, seed={}",
		argv[1], botConfig.botCount, botConfig.connectPerSecond, botConfig.randomSeed);
	JAM_LOG_INFO("M1_Bot configuration: ip={}, tcpPort={}, udpPort={}, warmUpMinutes={}, measurementMinutes={}",
		botConfig.serverIp, botConfig.tcpPort, botConfig.udpPort, warmUpMinutes, measurementMinutes);

	JAM_LOG_INFO("Commands: /stat + Enter to print stats, [q] to abort");

	auto nextPhaseCheck = std::chrono::steady_clock::now();
	auto nextStats = nextPhaseCheck;
	DWORD consoleMode = 0;
	const bool interactive = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &consoleMode) != 0;
	bool stopRequested = false;
	std::string command;
	bool allFailed = false;
	bool phaseFailed = false;
	bool aborted = false;
	while (true)
	{
		bool statsRequested = false;
		while (interactive && _kbhit())
		{
			const char input = static_cast<char>(_getch());
			if (input == '\0' || input == static_cast<char>(0xE0))
			{
				_getch();
				continue;
			}
			if (command.empty() && (input == 'q' || input == 'Q'))
			{
				aborted = true;
				break;
			}
			if (input == '\r')
			{
				std::cout << '\n';
				if (command == "/stat")
					statsRequested = true;
				else if (!command.empty())
					JAM_LOG_WARN("Unknown command: {} (use /stat)", command);
				command.clear();
			}
			else if (input == '\b' && !command.empty())
			{
				command.pop_back();
				std::cout << "\b \b" << std::flush;
			}
			else if (input >= ' ' && input <= '~' && command.size() < 64)
			{
				command.push_back(input);
				std::cout << input << std::flush;
			}
		}
		if (aborted)
			break;

		runner.Update();

		const auto now = std::chrono::steady_clock::now();
		if (now >= nextPhaseCheck)
		{
			if (!stopFile.empty())
			{
				std::error_code error;
				stopRequested = std::filesystem::exists(stopFile, error);
				if (error) { JAM_LOG_ERROR("Failed to inspect stop file: {}", error.message()); phaseFailed = true; break; }
				if (stopRequested) { aborted = true; break; }
			}
			const BotRunnerStats stats = runner.GetStats();
			nextPhaseCheck = now + std::chrono::seconds(1);

			if (runner.HasStartedAll() && stats.total != 0 && stats.failed == stats.total)
			{
				allFailed = true;
				break;
			}

			if (phase == eValidationPhase::RampUp)
			{
				if (runner.HasStartedAll() && stats.running + stats.failed == stats.total)
				{
					LogPhase(phase, "end");

					phase			= eValidationPhase::WarmUp;
					phaseStartedAt	= now;
					phaseDeadline	= now + warmUpDuration;
					
					LogPhase(phase, "start");
				}
				else if (now >= rampDeadline)
				{
					JAM_LOG_ERROR("Ramp-up timed out: started={}/{} running={} failed={}",
						stats.connectStarted, stats.total, stats.running, stats.failed);
					phaseFailed = true;
					break;
				}
			}
			else if (phase == eValidationPhase::WarmUp && now >= phaseDeadline)
			{
				LogPhase(phase, "end");
				
				phase		   = eValidationPhase::Measurement;
				phaseStartedAt = now;
				phaseDeadline  = now + measurementDuration;
				runner.BeginMeasurement();
				
				LogPhase(phase, "start");
			}
			else if (phase == eValidationPhase::Measurement && now >= phaseDeadline)
			{
				LogPhase(phase, "end");

				phase = eValidationPhase::CoolDown;
				phaseStartedAt = now;
				
				LogPhase(phase, "start");
				break;
			}
		}
		if (statsRequested || (statsIntervalSeconds != 0 && now >= nextStats))
		{
			PrintStats(runner.GetStats(), phase, now - runStartedAt, now - phaseStartedAt);
			nextStats = now + std::chrono::seconds(statsIntervalSeconds);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	const BotRunnerStats finalStats = runner.GetStats();
	if (aborted || allFailed || phaseFailed || phase != eValidationPhase::CoolDown)
	{
		JAM_LOG_WARN("M1_Bot phase aborted: phase={} reason={} running={} failed={}",
			PhaseName(phase), allFailed ? "all-failed" : (aborted ? (stopRequested ? "stop-file" : "user") : "phase-failure"), finalStats.running, finalStats.failed);
		LogPhase(phase, "end");
		
		phase = eValidationPhase::CoolDown;
		phaseStartedAt = std::chrono::steady_clock::now();
		
		LogPhase(phase, "start");
	}

	JAM_LOG_INFO("M1_Bot stopped: running={}, failed={}", finalStats.running, finalStats.failed);

	runner.Close();
	
	const auto coolDownEndedAt = std::chrono::steady_clock::now();
	const bool validationFailed = aborted || allFailed || phaseFailed || finalStats.failed != 0;

	LogPhase(eValidationPhase::CoolDown, "end");

	JAM_LOG_INFO("M1_Bot validation finished: totalDurationMs={} result={}",
		std::chrono::duration_cast<std::chrono::milliseconds>(coolDownEndedAt - runStartedAt).count(), validationFailed ? "failed" : "completed");

	return validationFailed ? -1 : 0;
}
