#include "pch.h"
#include "BotRunner.h"

#include <SharedData/Cpp/world_contents.generated.hpp>


namespace
{
	constexpr jam::net::AccountId kBotAccountBegin = 6000;
	constexpr jam::net::AccountId kBotAccountEnd = 9999;

	BotScenarioConfig MakeProfileScenario(eBotProfile profile, uint32 botIndex, uint32 botCount)
	{
		BotScenarioConfig config{};
		config.movement.enabled = true;
		config.movement.pattern = eBotMovementPattern::Traverse;
		config.movement.portalMode = eBotPortalMode::Distributed;

		uint32 globalEnd = 0;
		uint32 worldEnd = 0;
		switch (profile)
		{
		case eBotProfile::Baseline:
			config.chat.intervalMs		 = 10'000;
			config.chat.textLength		 = 64;
			config.movement.moveDuration = { .fixedMs = 1'000 };
			config.movement.idleDuration = { 1'000, 1'000 };
			config.movement.worldStayDuration = { 20'000, 30'000 };
			
			globalEnd = (botCount * 3 + 9) / 10;
			worldEnd  = (botCount * 8 + 9) / 10;
			break;

		case eBotProfile::Target:
			config.chat.intervalMs		 = 10'000;
			config.chat.textLength		 = 64;
			config.movement.moveDuration = { .fixedMs = 1'000 };
			config.movement.idleDuration = { 1'000, 1'000 };
			config.movement.worldStayDuration = { 20'000, 30'000 };

			//globalEnd = (botCount * 3 + 9) / 10;
			globalEnd = 0;
			worldEnd  = (botCount * 8 + 9) / 10;
			break;

		case eBotProfile::Peak:
			config.chat.intervalMs		 = 2'000;
			config.chat.textLength		 = botIndex % 2 == 0 ? 64 : 128;
			config.movement.moveDuration = { .fixedMs = 4'000 };
			config.movement.idleDuration = { 1'000, 250 };
			
			globalEnd = (botCount * 4 + 9) / 10;
			worldEnd  = (botCount * 9 + 9) / 10;
			break;

		case eBotProfile::Stress:
			config.chat.intervalMs		 = 1'000;
			config.chat.textLength		 = 128;
			config.movement.moveDuration = { .fixedMs = 5'000 };
			config.movement.idleDuration = {};

			globalEnd = (botCount * 5 + 9) / 10;
			worldEnd  = botCount;
			break;

		case eBotProfile::AoiHotspot:
			config.chat.channel			 = eBotChatChannel::Disabled;
			config.movement.pattern		 = eBotMovementPattern::ClusterSquare;
			config.movement.portalMode	 = eBotPortalMode::Disabled;
			config.movement.moveDuration = { .fixedMs = 4'000 };
			config.movement.idleDuration = { 1'000, 250 };
			return config;
		
			case eBotProfile::PortalBurst:
			config.chat.channel			= eBotChatChannel::Disabled;
			config.movement.pattern		= eBotMovementPattern::Idle;
			config.movement.portalMode	= eBotPortalMode::Synchronized;
			return config;

		default:
			return config;
		}

		config.chat.channel = botIndex < globalEnd ? eBotChatChannel::Global : (botIndex < worldEnd ? eBotChatChannel::World : eBotChatChannel::Direct);

		return config;
	}

	struct LoadedScenarioData
	{
		std::shared_ptr<BotPortalApproaches> portalApproaches = std::make_shared<BotPortalApproaches>();
		std::shared_ptr<BotScenarioLayout> layout = std::make_shared<BotScenarioLayout>();
		std::vector<std::pair<std::string, jam::net::WorldArchetypeKey>> initialWorlds;
	};

	LoadedScenarioData LoadScenarioData(const std::filesystem::path& path)
	{
		const auto dto = jam::shared::gen::LoadWorldContentsRootDto(path);
		LoadedScenarioData result;
		for (const auto& [worldName, world] : dto.worlds)
		{
			for (const auto& lane : world.scenario.traverseLanes)
			{
				if (lane.start.size() != 3 || lane.direction.size() != 2 || lane.length <= 0.0f)
					throw std::runtime_error("invalid bot traverse lane: " + worldName);

				result.layout->traverseLanes.push_back({
					.start		= jam::px::Vec3(lane.start[0], lane.start[1], lane.start[2]),
					.direction	= { .localX = lane.direction[0], .localY = lane.direction[1] },
					.length		= lane.length,
				});
			}
			for (const auto& hotspot : world.scenario.hotspots)
			{
				if (hotspot.center.size() != 3 || hotspot.halfExtents.size() != 2)
					throw std::runtime_error("invalid bot hotspot: " + worldName);

				result.layout->hotspots.push_back({
					.center		 = jam::px::Vec3(hotspot.center[0], hotspot.center[1], hotspot.center[2]),
					.halfExtentX = hotspot.halfExtents[0],
					.halfExtentZ = hotspot.halfExtents[1],
				});
			}
		}

		for (const auto& [instanceName, instance] : dto.instances)
		{
			result.initialWorlds.emplace_back(instanceName, jam::net::MakeWorldArchetypeKey(instance.worldArchetype));
			const auto worldIt = dto.worlds.find(instance.worldArchetype);
			if (worldIt == dto.worlds.end())
				throw std::runtime_error("missing bot world contents: " + instanceName);

			auto& approaches = (*result.portalApproaches)[jam::net::MakeStaticWorldInstanceId(instanceName)];
			for (const auto& portal : worldIt->second.portals)
			{
				const auto routeIt = instance.portalDestinations.find(portal.name);
				if (routeIt == instance.portalDestinations.end() || portal.approachPosition.size() != 3)
					throw std::runtime_error("invalid bot portal route: " + instanceName + "/" + portal.name);

				approaches.push_back({
					.destinationWorld = {
						.instanceId   = jam::net::MakeStaticWorldInstanceId(routeIt->second.destinationName),
						.archetypeKey = jam::net::MakeWorldArchetypeKey(routeIt->second.worldArchetype),
					},
					.position = jam::px::Vec3(portal.approachPosition[0], portal.approachPosition[1], portal.approachPosition[2]),
				});
			}
		}
		std::ranges::sort(result.initialWorlds, [](const auto& lhs, const auto& rhs)
			{
				return lhs.first < rhs.first;
			});
		return result;
	}
}


BotRunner::~BotRunner()
{
	Shutdown();
}

bool BotRunner::Init(const BotRunnerConfig& config)
{
	if (m_initialized || config.botCount == 0 || config.botCount > kBotAccountEnd - kBotAccountBegin + 1)
	{
		return false;
	}

	BotRunnerConfig effectiveConfig = config;
	std::vector<std::pair<std::string, jam::net::WorldArchetypeKey>> initialWorlds;
	if (effectiveConfig.profile != eBotProfile::Custom)
		effectiveConfig.scenarioConfig = MakeProfileScenario(effectiveConfig.profile, 0, effectiveConfig.botCount);

	if (effectiveConfig.scenarioConfig.movement.enabled
		&& (!effectiveConfig.scenarioConfig.movement.scenarioLayout || (effectiveConfig.scenarioConfig.movement.portalMode != eBotPortalMode::Disabled && !effectiveConfig.scenarioConfig.movement.portalApproaches)))
	{
		try
		{
			const std::filesystem::path manifestPath(effectiveConfig.sharedDataManifestPath);
			const LoadedScenarioData loaded = LoadScenarioData(manifestPath.parent_path() / "World" / "world_contents.json");

			effectiveConfig.scenarioConfig.movement.portalApproaches = loaded.portalApproaches;
			effectiveConfig.scenarioConfig.movement.scenarioLayout = loaded.layout;
			initialWorlds = loaded.initialWorlds;
		}
		catch (const std::exception& error)
		{
			JAM_LOG_ERROR("Failed to load bot portal approaches: {}", error.what());
			return false;
		}
	}

	if (!effectiveConfig.scenarioConfig.movement.IsValid())
		return false;

	m_config = effectiveConfig;
	m_slots.reserve(config.botCount);

	for (uint32 i = 0; i < config.botCount; ++i)
	{
		auto slot = std::make_unique<BotSlot>();
		slot->client = std::make_unique<BotClient>();
		BotScenarioConfig scenarioConfig = effectiveConfig.profile == eBotProfile::Custom ? effectiveConfig.scenarioConfig : MakeProfileScenario(effectiveConfig.profile, i, effectiveConfig.botCount);

		scenarioConfig.movement.portalApproaches = effectiveConfig.scenarioConfig.movement.portalApproaches;
		scenarioConfig.movement.scenarioLayout	 = effectiveConfig.scenarioConfig.movement.scenarioLayout;
		scenarioConfig.movement.randomSeed		 = effectiveConfig.randomSeed + i;

		if (effectiveConfig.profile != eBotProfile::Custom && !initialWorlds.empty())
		{
			const auto& [name, archetype] = initialWorlds[i % initialWorlds.size()];
			scenarioConfig.initialWorldName		 = name;
			scenarioConfig.initialWorldArchetype = archetype;
		}

		if (scenarioConfig.chat.channel == eBotChatChannel::Direct)
		{
			const bool targetCohort = effectiveConfig.profile == eBotProfile::Baseline || effectiveConfig.profile == eBotProfile::Target;
			const uint32 directBegin = targetCohort ? (config.botCount * 8 + 9) / 10 : (effectiveConfig.profile == eBotProfile::Peak ? (config.botCount * 9 + 9) / 10 : 0);
			const uint32 directCount = config.botCount - directBegin;
			if (directCount < 2)
			{
				Shutdown();
				return false;
			}

			const uint32 targetIndex = directBegin + ((i - directBegin + 1) % directCount);
			const jam::net::AccountId targetAccountId = kBotAccountBegin + targetIndex;
			
			scenarioConfig.chat.directTargetName = "Bot" + std::to_string(targetAccountId);
		}

		if (!slot->scenario.Configure(scenarioConfig))
		{
			Shutdown();
			return false;
		}

		const jam::net::AccountId accountId = kBotAccountBegin + i;
		jam::net::ClientConfig clientConfig{};

		clientConfig.accountId				= accountId;
		clientConfig.serverTcpAddress		= jam::net::NetAddress(config.serverIp, config.tcpPort);
		clientConfig.serverUdpAddress		= jam::net::NetAddress(config.serverIp, config.udpPort);
		clientConfig.sharedDataManifestPath = config.sharedDataManifestPath;

		if (!slot->client->Init(std::move(clientConfig)))
		{
			Shutdown();
			return false;
		}

		m_slots.push_back(std::move(slot));
	}

	m_initialized = true;
	return true;
}

void BotRunner::Start()
{
	if (!m_initialized || m_running)
		return;

	m_nextConnectIndex	= 0;
	m_nextConnectTime	= std::chrono::steady_clock::now();
	m_running			= true;
	m_synchronizedPortalStarted = false;

	StartPendingConnections();
}

void BotRunner::Update()
{
	if (!m_running)
		return;

	StartPendingConnections();

	for (auto& slot : m_slots)
	{
		if (!slot->connectStarted || slot->connectRejected)
			continue;

		slot->client->Pump(m_config.pumpOptions);

		jam::net::ClientEvent event;
		while (slot->client->PollEvent(event))
		{
			slot->scenario.OnEvent(*slot->client, event);
		}

		slot->scenario.Update(*slot->client);
	}

	if (HasStartedAll())
	{
		const bool allRunning = std::ranges::all_of(m_slots, [](const auto& slot)
			{
				return slot->scenario.IsRunning();
			});

		if (allRunning)
		{
			for (auto& slot : m_slots)
				slot->scenario.EnableDirectChat(*slot->client);

		}
	}
}

void BotRunner::BeginMeasurement()
{
	if (!m_running || m_config.profile != eBotProfile::PortalBurst || m_synchronizedPortalStarted)
		return;

	for (auto& slot : m_slots)
	{
		if (slot->scenario.IsRunning())
			slot->scenario.BeginSynchronizedPortal(*slot->client);
	}
	m_synchronizedPortalStarted = true;
}

void BotRunner::Shutdown()
{
	// Start every graceful disconnect before waiting for any individual client.
	// ClientRuntime::Shutdown() is synchronous, so issuing it directly per slot
	// serializes the UDP close timeout across the entire bot population.
	for (auto& slot : m_slots)
	{
		if (slot && slot->client)
			slot->client->Disconnect();
	}

	for (auto& slot : m_slots)
	{
		if (slot && slot->client)
			slot->client->Shutdown();
	}

	m_slots.clear();
	m_nextConnectIndex = 0;
	m_running = false;
	m_synchronizedPortalStarted = false;
	m_initialized = false;
}

BotRunnerStats BotRunner::GetStats() const
{
	BotRunnerStats stats{};
	stats.total			= static_cast<uint32>(m_slots.size());

	for (const auto& slot : m_slots)
	{
		if (slot->connectStarted)
			++stats.connectStarted;
		if (slot->connectRejected)
			++stats.connectRejected;
		if (slot->client->GetState() == eBotState::Connecting)
			++stats.connecting;
		if (slot->client->IsReady())
			++stats.ready;
		if (slot->client->IsInWorld())
			++stats.inWorld;
		if (slot->scenario.IsRunning())
			++stats.running;
		if (slot->connectRejected || slot->scenario.HasFailed())
			++stats.failed;

		stats.chatSent			+= slot->scenario.GetChatSent();
		stats.chatRejected		+= slot->scenario.GetChatRejected();
		stats.chatReceived		+= slot->scenario.GetChatReceived();
		stats.portalTransitions += slot->scenario.GetPortalTransitions();
		stats.portalTimeouts	+= slot->scenario.GetPortalTimeouts();
		stats.unexpectedWorldTransitions += slot->scenario.GetUnexpectedWorldTransitions();
	}

	return stats;
}

BotClient* BotRunner::GetClient(uint32 index)
{
	return index < m_slots.size() ? m_slots[index]->client.get() : nullptr;
}

BotScenario* BotRunner::GetScenario(uint32 index)
{
	return index < m_slots.size() ? &m_slots[index]->scenario : nullptr;
}

void BotRunner::StartPendingConnections()
{
	if (m_nextConnectIndex >= m_slots.size())
		return;

	const auto now = std::chrono::steady_clock::now();
	const auto interval = m_config.connectPerSecond == 0 ? std::chrono::steady_clock::duration::zero() : std::chrono::nanoseconds(1'000'000'000ull / m_config.connectPerSecond);

	while (m_nextConnectIndex < m_slots.size() && (m_config.connectPerSecond == 0 || now >= m_nextConnectTime))
	{
		BotSlot& slot = *m_slots[m_nextConnectIndex++];
		slot.connectStarted = true;

		if (slot.client->Connect())
		{
			slot.scenario.Start();
		}
		else
		{
			slot.connectRejected = true;
		}

		if (m_config.connectPerSecond != 0)
			m_nextConnectTime += interval;
	}
}
