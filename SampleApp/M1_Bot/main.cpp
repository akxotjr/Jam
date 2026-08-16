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
		std::cout << "Usage: M1_Bot.exe baseline\n";
		std::cout << "Usage: M1_Bot.exe <target|peak|stress|aoi-hotspot|portal-burst> <botCount>\n";
		std::cout << "  baseline: 300 bots, 3-minute measurement\n";
		std::cout << "  botCount: 1-4000\n";
	}

	enum class eValidationPhase
	{
		RampUp,
		WarmUp,
		Measurement,
		CoolDown,
		Complete,
	};

	std::string_view PhaseName(eValidationPhase phase)
	{
		switch (phase)
		{
		case eValidationPhase::RampUp:     return "ramp-up";
		case eValidationPhase::WarmUp:     return "warm-up";
		case eValidationPhase::Measurement:return "measurement";
		case eValidationPhase::CoolDown:   return "cool-down";
		case eValidationPhase::Complete:   return "complete";
		default:                           return "unknown";
		}
	}

	uint64 UnixNowNs()
	{
		return static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
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

	void PrintStats(const BotRunnerStats& stats)
	{
		JAM_LOG_INFO(
			"[M1BotStats] started={}/{}, connectRejected={}, connecting={}, ready={}, inWorld={}, running={}, failed={}, "
			"chatTx={}, chatRx={}, chatRejected={}, portal={}, portalTimeout={}, unexpectedWorld={}",
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
	const bool baseline = argc == 2 && std::string_view(argv[1]) == "baseline";
	const auto profile = (baseline || argc == 3) ? ParseProfile(argv[1]) : std::nullopt;
	if (baseline)
		botConfig.botCount = 300;
	else if (argc == 3 && !ParseUInt32(argv[2], botConfig.botCount))
	{
		PrintUsage();
		return -1;
	}
	if (!profile
		|| botConfig.botCount == 0 || botConfig.botCount > 4000)
	{
		PrintUsage();
		return -1;
	}
	botConfig.profile = *profile;
	botConfig.connectPerSecond = 100;
	botConfig.randomSeed = 1;

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
	jam::net::NetRuntime runtime(runtimeConfig);
	if (!runtime.IsInitialized())
	{
		JAM_LOG_ERROR("Failed to initialize NetRuntime");
		return -1;
	}

	BotRunner runner;
	if (!runner.Init(botConfig))
	{
		JAM_LOG_ERROR("Failed to initialize BotRunner");
		return -1;
	}

	runner.Start();

	constexpr auto kRampSettleTimeout = std::chrono::minutes(5);

	const auto warmUpDuration		 = baseline ? std::chrono::minutes(1) : std::chrono::minutes(5);
	const auto measurementDuration	 = baseline ? std::chrono::minutes(3) : std::chrono::minutes(30);
	const auto runStartedAt			 = std::chrono::steady_clock::now();
	const auto scheduledRampDuration = std::chrono::seconds((botConfig.botCount + botConfig.connectPerSecond - 1) / botConfig.connectPerSecond);
	const auto rampDeadline			 = runStartedAt + scheduledRampDuration + kRampSettleTimeout;

	eValidationPhase phase = eValidationPhase::RampUp;
	
	auto phaseStartedAt = runStartedAt;
	auto phaseDeadline = rampDeadline;
	
	JAM_LOG_INFO("[M1ValidationPhase] phase={} event=begin unix_ns={} bots={} profile={} rampRate={} seed={} warmupSec={} measurementSec={}",
		PhaseName(phase), UnixNowNs(), botConfig.botCount, argv[1], botConfig.connectPerSecond, botConfig.randomSeed,
		std::chrono::duration_cast<std::chrono::seconds>(warmUpDuration).count(),
		std::chrono::duration_cast<std::chrono::seconds>(measurementDuration).count());

	JAM_LOG_INFO("M1_Bot started: profile={}, bots={}, connectPerSecond={}, seed={}",
		argv[1], botConfig.botCount, botConfig.connectPerSecond, botConfig.randomSeed);

	JAM_LOG_INFO("Press [q] to abort");

	auto nextReport = std::chrono::steady_clock::now();
	bool allFailed = false;
	bool phaseFailed = false;
	bool aborted = false;
	while (true)
	{
		if (_kbhit())
		{
			const char input = static_cast<char>(_getch());
			if (input == 'q' || input == 'Q')
			{
				aborted = true;
				break;
			}
		}

		runner.Update();

		const auto now = std::chrono::steady_clock::now();
		if (now >= nextReport)
		{
			const BotRunnerStats stats = runner.GetStats();
			PrintStats(stats);
			nextReport = now + std::chrono::seconds(1);

			if (runner.HasStartedAll() && stats.total != 0 && stats.failed == stats.total)
			{
				allFailed = true;
				break;
			}

			if (phase == eValidationPhase::RampUp)
			{
				if (runner.HasStartedAll() && stats.running + stats.failed == stats.total)
				{
					JAM_LOG_INFO("[M1ValidationPhase] phase={} event=end unix_ns={} durationMs={} running={} failed={}",
						PhaseName(phase), UnixNowNs(), std::chrono::duration_cast<std::chrono::milliseconds>(now - phaseStartedAt).count(), stats.running, stats.failed);

					phase			= eValidationPhase::WarmUp;
					phaseStartedAt	= now;
					phaseDeadline	= now + warmUpDuration;
					
					JAM_LOG_INFO("[M1ValidationPhase] phase={} event=begin unix_ns={}", PhaseName(phase), UnixNowNs());
				}
				else if (now >= rampDeadline)
				{
					JAM_LOG_ERROR("[M1ValidationPhase] phase={} event=timeout unix_ns={} durationMs={} started={}/{} running={} failed={}",
						PhaseName(phase), UnixNowNs(), std::chrono::duration_cast<std::chrono::milliseconds>(now - phaseStartedAt).count(), stats.connectStarted, stats.total, stats.running, stats.failed);
					phaseFailed = true;
					break;
				}
			}
			else if (phase == eValidationPhase::WarmUp && now >= phaseDeadline)
			{
				JAM_LOG_INFO("[M1ValidationPhase] phase={} event=end unix_ns={} durationMs={}", PhaseName(phase), UnixNowNs(), std::chrono::duration_cast<std::chrono::milliseconds>(now - phaseStartedAt).count());
				
				phase		   = eValidationPhase::Measurement;
				phaseStartedAt = now;
				phaseDeadline  = now + measurementDuration;
				runner.BeginMeasurement();
				
				JAM_LOG_INFO("[M1ValidationPhase] phase={} event=begin unix_ns={}", PhaseName(phase), UnixNowNs());
			}
			else if (phase == eValidationPhase::Measurement && now >= phaseDeadline)
			{
				JAM_LOG_INFO("[M1ValidationPhase] phase={} event=end unix_ns={} durationMs={} running={} failed={}",
					PhaseName(phase), UnixNowNs(), std::chrono::duration_cast<std::chrono::milliseconds>(now - phaseStartedAt).count(), stats.running, stats.failed);

				phase = eValidationPhase::CoolDown;
				phaseStartedAt = now;
				
				JAM_LOG_INFO("[M1ValidationPhase] phase={} event=begin unix_ns={}", PhaseName(phase), UnixNowNs());
				break;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	const BotRunnerStats finalStats = runner.GetStats();
	if (aborted || allFailed || phaseFailed || phase != eValidationPhase::CoolDown)
	{
		JAM_LOG_WARN("[M1ValidationPhase] phase={} event=aborted unix_ns={} reason={} running={} failed={}",
			PhaseName(phase), UnixNowNs(), allFailed ? "all-failed" : (aborted ? "user" : "ramp-timeout"), finalStats.running, finalStats.failed);
		
		phase = eValidationPhase::CoolDown;
		phaseStartedAt = std::chrono::steady_clock::now();
		
		JAM_LOG_INFO("[M1ValidationPhase] phase={} event=begin unix_ns={}", PhaseName(phase), UnixNowNs());
	}

	JAM_LOG_INFO("M1_Bot stopped: running={}, failed={}", finalStats.running, finalStats.failed);

	runner.Shutdown();
	
	const auto coolDownEndedAt = std::chrono::steady_clock::now();
	const bool validationFailed = aborted || allFailed || phaseFailed || finalStats.failed != 0;

	JAM_LOG_INFO("[M1ValidationPhase] phase={} event=end unix_ns={} durationMs={}", PhaseName(eValidationPhase::CoolDown), UnixNowNs(),
		std::chrono::duration_cast<std::chrono::milliseconds>(coolDownEndedAt - phaseStartedAt).count());

	JAM_LOG_INFO("[M1ValidationPhase] phase={} event=end unix_ns={} totalDurationMs={} result={}",
		PhaseName(eValidationPhase::Complete), UnixNowNs(), std::chrono::duration_cast<std::chrono::milliseconds>(coolDownEndedAt - runStartedAt).count(), validationFailed ? "failed" : "completed");

	return validationFailed ? -1 : 0;
}
