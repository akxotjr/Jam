#pragma once

#include "BotClient.h"
#include "BotScenario.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

enum class eBotProfile : uint8
{
	Custom,
	Baseline,
	Target,
	Peak,
	Stress,
	AoiHotspot,
	PortalBurst,
};

struct BotRunnerConfig
{
	uint32						botCount			   = 1;
	uint32						connectPerSecond	   = 50;
	std::string					serverIp			   = "127.0.0.1";
	uint16						tcpPort				   = 7777;
	uint16						udpPort				   = 8888;
	std::string					sharedDataManifestPath = "C://Users//akxotjr//GameWorkSpace//Jam-dev//SampleApp//M1_Shared//Data//shared_data_manifest.json";
	jam::net::ClientPumpOptions pumpOptions			   = {};
	BotScenarioConfig			scenarioConfig		   = {};
	eBotProfile					profile				   = eBotProfile::Custom;
	uint32						randomSeed			   = 1;
};

struct BotRunnerStats
{
	uint32 total						= 0;
	uint32 connectStarted				= 0;
	uint32 connecting					= 0;
	uint32 ready						= 0;
	uint32 inWorld						= 0;
	uint32 running						= 0;
	uint32 failed						= 0;
	uint32 connectRejected				= 0;
	uint64 chatSent						= 0;
	uint64 chatRejected					= 0;
	uint64 chatReceived					= 0;
	uint64 portalTransitions			= 0;
	uint64 portalTimeouts				= 0;
	uint64 unexpectedWorldTransitions	= 0;
};


class BotRunner
{
public:
	BotRunner() = default;
	~BotRunner();

	bool			Init(const BotRunnerConfig& config);
	void			Start();
	void			Update();
	void			BeginMeasurement();
	void			Shutdown();

	bool			IsInitialized() const { return m_initialized; }
	bool			IsRunning()     const { return m_running; }
	bool			HasStartedAll() const { return m_nextConnectIndex >= m_slots.size(); }

	BotRunnerStats	GetStats() const;
	BotClient*		GetClient(uint32 index);
	BotScenario*	GetScenario(uint32 index);

private:
	struct BotSlot
	{
		std::unique_ptr<BotClient>	client;
		BotScenario					scenario;
		bool						connectStarted = false;
		bool						connectRejected = false;
	};

	void StartPendingConnections();

private:
	BotRunnerConfig							m_config			= {};
	std::vector<std::unique_ptr<BotSlot>>	m_slots;
	size_t									m_nextConnectIndex	= 0;
	std::chrono::steady_clock::time_point	m_nextConnectTime	= {};
	bool									m_initialized		= false;
	bool									m_running			= false;
	bool									m_synchronizedPortalStarted = false;
};
