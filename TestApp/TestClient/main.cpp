#include "pch.h"

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include <fstream>
#include <future>

#include "ClientInstance.h"
#include "UserInstance.h"
#include "BotInstance.h"
#include "Renderer.h"
#include "jampx/PhysicsCore.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/prefab/PrefabCooker.h"
#include "jampx/prefab/PrefabDocument.h"

#include "jamnet/runtime/ClientSession.h"


using namespace jam::net;


struct TestConfig
{
    uint32 numUsers     = 1;
    uint32 numBots      = 0;
    string serverIp     = "127.0.0.1";
    uint16 tcpPort      = 7777;
    uint16 udpPort      = 8888;
};

// 고해상도 슬립 보조: 남은 시간이 임계값 이상이면 sleep_for 후 마무리는 얕은 스핀/양보
static void PreciseFrameSleep(std::chrono::steady_clock::time_point frameStart,
                                     std::chrono::nanoseconds targetSpan,
                                     std::chrono::nanoseconds sleepGuard,
                                     double& outSleepReqMs,
                                     double& outSleepActMs,
                                     double& outSpinMs)
{
    using clock = std::chrono::steady_clock;

    outSleepReqMs = 0.0;
    outSleepActMs = 0.0;
    outSpinMs     = 0.0;

    const auto beforeSleep = clock::now();
    auto elapsed = beforeSleep - frameStart;

    if (elapsed < targetSpan)
    {
        auto remaining = targetSpan - elapsed;
        auto toSleep = (remaining > sleepGuard) ? (remaining - sleepGuard) : std::chrono::nanoseconds(0);

        if (toSleep.count() > 0)
        {
            const auto s0 = clock::now();
            outSleepReqMs = std::chrono::duration<double, std::milli>(toSleep).count();
            std::this_thread::sleep_for(toSleep);
            const auto s1 = clock::now();
            outSleepActMs = std::chrono::duration<double, std::milli>(s1 - s0).count();
        }

        const auto spinStart = clock::now();
        while (clock::now() - frameStart < targetSpan)
        {
            std::this_thread::yield();
        }
        const auto spinEnd = clock::now();
        outSpinMs = std::chrono::duration<double, std::milli>(spinEnd - spinStart).count();
    }
}

struct ScopedTimerResolution
{
    UINT m_val{1};
    ScopedTimerResolution() { timeBeginPeriod(m_val); }
    ~ScopedTimerResolution() { timeEndPeriod(m_val); }
};

namespace
{
	struct SessionMetricsRow
	{
		uint64 captureEpochMs = 0;

		uint32 clientInstanceId = 0;
		uint64 userId = 0;
		uint64 sessionId = 0;
		uint32 entity = 0;
		std::string protocol;

		float rtt_ms = 0.0f;
		float jitter_ms = 0.0f;
		float packetLoss = 0.0f;
		float recvThroughput_kbps = 0.0f;
		float sendThroughput_kbps = 0.0f;
		float bandwidthMbps = 0.0f;

		float txPacketsPerSec = 0.0f;
		float rxPacketsPerSec = 0.0f;
		float txBytesPerSec = 0.0f;
		float rxBytesPerSec = 0.0f;

		float firstSendSuccessPct = 0.0f;
		float rtxHitPct = 0.0f;
		float rtxRecoveryPct = 0.0f;
		float avgRtxPerHitPacket = 0.0f;

		float goodputPct = 0.0f;
		float fragEfficiencyPct = 0.0f;
		float ackPiggybackHitPct = 0.0f;

		float outOfOrderPct = 0.0f;
		float duplicatePct = 0.0f;

		uint32 pendingReliableNow = 0;
		uint32 pendingReliablePeek = 0;
		uint32 maxRtxPerPacket = 0;

		float rtxTimeoutPct = 0.0f;
		float rtxGiveupPct = 0.0f;
	};

	static uint64 NowEpochMs()
	{
		const auto now = std::chrono::system_clock::now();
		return static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
	}

	static std::string MakeRunTimestamp()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t tt = std::chrono::system_clock::to_time_t(now);
		std::tm local{};
		localtime_s(&local, &tt);

		std::ostringstream oss;
		oss << std::put_time(&local, "%Y%m%d_%H%M%S");
		return oss.str();
	}

	static std::optional<SessionMetricsRow> CaptureSessionRow(
		const std::shared_ptr<Session>& session,
		uint32 clientInstanceId,
		uint64 userId,
		const char* protocolTag)
	{
		if (!session)
			return std::nullopt;

		auto promise = std::make_shared<std::promise<std::optional<SessionMetricsRow>>>();
		auto future = promise->get_future();

		session->Post(Job([session, clientInstanceId, userId, protocol = std::string(protocolTag), promise]()
			{
				try
				{
					auto& L = SHARD_LOCAL_CHECKED();
					auto& R = L.registry;

					const entt::entity e = session->GetEntity();
					if (e == entt::null || !R.valid(e))
					{
						promise->set_value(std::nullopt);
						return;
					}

					SessionMetricsRow row{};
					row.captureEpochMs = NowEpochMs();
					row.clientInstanceId = clientInstanceId;
					row.userId = userId;
					row.sessionId = session->GetSessionId();
					row.entity = static_cast<uint32>(e);
					row.protocol = protocol;

					const auto netView = profile::NetworkStatsView::FromEntity(R, e);
					const auto kpiView = profile::RudpKpiView::FromEntity(R, e);

					row.rtt_ms = netView.rtt_ms;
					row.jitter_ms = netView.jitter_ms;
					row.packetLoss = netView.packetLoss;
					row.recvThroughput_kbps = netView.recvThroughput_kbps;
					row.sendThroughput_kbps = netView.sendThroughput_kbps;
					row.bandwidthMbps = netView.bandwidthMbps;

					row.txPacketsPerSec = kpiView.txPacketsPerSec;
					row.rxPacketsPerSec = kpiView.rxPacketsPerSec;
					row.txBytesPerSec = kpiView.txBytesPerSec;
					row.rxBytesPerSec = kpiView.rxBytesPerSec;

					row.firstSendSuccessPct = kpiView.firstSendSuccessPct;
					row.rtxHitPct = kpiView.rtxHitPct;
					row.rtxRecoveryPct = kpiView.rtxRecoveryPct;
					row.avgRtxPerHitPacket = kpiView.avgRtxPerHitPacket;

					row.goodputPct = kpiView.goodputPct;
					row.fragEfficiencyPct = kpiView.fragEfficiencyPct;
					row.ackPiggybackHitPct = kpiView.ackPiggybackHitPct;

					row.outOfOrderPct = kpiView.outOfOrderPct;
					row.duplicatePct = kpiView.duplicatePct;

					row.pendingReliableNow = kpiView.pendingReliableNow;
					row.pendingReliablePeek = kpiView.pendingReliablePeek;
					row.maxRtxPerPacket = kpiView.maxRtxPerPacket;

					row.rtxTimeoutPct = kpiView.rtxTimeoutPct;
					row.rtxGiveupPct = kpiView.rtxGiveupPct;

					promise->set_value(row);
				}
				catch (...)
				{
					promise->set_value(std::nullopt);
				}
			}));

		if (future.wait_for(std::chrono::milliseconds(80)) != std::future_status::ready)
			return std::nullopt;

		return future.get();
	}

	class SessionCsvReporter
	{
	public:
		SessionCsvReporter()
		{
			const std::string stamp = MakeRunTimestamp();
			m_dir = std::filesystem::path("Reports") / "NetworkProfile";
			std::filesystem::create_directories(m_dir);

			m_csvPath = m_dir / ("session_metrics_" + stamp + ".csv");
			m_summaryPath = m_dir / ("session_metrics_" + stamp + "_summary.csv");

			m_csv.open(m_csvPath, std::ios::out | std::ios::trunc);
			WriteHeader();
		}

		~SessionCsvReporter()
		{
			if (m_csv.is_open())
				m_csv.flush();
		}

		void DumpOnce(const std::vector<std::unique_ptr<ClientInstance>>& clients)
		{
			if (!m_csv.is_open())
				return;

			for (const auto& client : clients)
			{
				if (!client)
					continue;

				auto* nm = client->GetNetworkManager();
				if (!nm)
					continue;

				if (auto row = CaptureSessionRow(nm->GetUdpSession(), client->GetInstanceId(), client->GetUserId(), "UDP"); row.has_value())
					WriteRow(row.value());

				if (auto row = CaptureSessionRow(nm->GetTcpSession(), client->GetInstanceId(), client->GetUserId(), "TCP"); row.has_value())
					WriteRow(row.value());
			}

			m_csv.flush();
		}

		void WriteSummary()
		{
			struct Agg
			{
				uint64 count = 0;
				double sum_rtt = 0.0;
				double sum_jitter = 0.0;
				double sum_loss = 0.0;
				double sum_bw = 0.0;
				double sum_txpps = 0.0;
				double sum_rxpps = 0.0;
				double sum_rtxHit = 0.0;
				double sum_rtxRecovery = 0.0;
				double sum_goodput = 0.0;
				double max_pending = 0.0;
				double max_rtxPerPacket = 0.0;
			};

			std::unordered_map<std::string, Agg> table;
			for (const auto& row : m_rows)
			{
				const std::string key = std::to_string(row.clientInstanceId) + "|" + std::to_string(row.userId) + "|" + row.protocol + "|" + std::to_string(row.sessionId);
				auto& a = table[key];
				a.count++;
				a.sum_rtt += row.rtt_ms;
				a.sum_jitter += row.jitter_ms;
				a.sum_loss += row.packetLoss;
				a.sum_bw += row.bandwidthMbps;
				a.sum_txpps += row.txPacketsPerSec;
				a.sum_rxpps += row.rxPacketsPerSec;
				a.sum_rtxHit += row.rtxHitPct;
				a.sum_rtxRecovery += row.rtxRecoveryPct;
				a.sum_goodput += row.goodputPct;
				a.max_pending = std::max(a.max_pending, static_cast<double>(row.pendingReliableNow));
				a.max_rtxPerPacket = std::max(a.max_rtxPerPacket, static_cast<double>(row.maxRtxPerPacket));
			}

			std::ofstream summary(m_summaryPath, std::ios::out | std::ios::trunc);
			if (!summary.is_open())
				return;

			summary << "key,samples,avg_rtt_ms,avg_jitter_ms,avg_packetLoss,avg_bandwidthMbps,avg_txPacketsPerSec,avg_rxPacketsPerSec,avg_rtxHitPct,avg_rtxRecoveryPct,avg_goodputPct,max_pendingReliableNow,max_maxRtxPerPacket\n";
			for (const auto& [key, a] : table)
			{
				const double n = (a.count == 0) ? 1.0 : static_cast<double>(a.count);
				summary
					<< key << ","
					<< a.count << ","
					<< (a.sum_rtt / n) << ","
					<< (a.sum_jitter / n) << ","
					<< (a.sum_loss / n) << ","
					<< (a.sum_bw / n) << ","
					<< (a.sum_txpps / n) << ","
					<< (a.sum_rxpps / n) << ","
					<< (a.sum_rtxHit / n) << ","
					<< (a.sum_rtxRecovery / n) << ","
					<< (a.sum_goodput / n) << ","
					<< a.max_pending << ","
					<< a.max_rtxPerPacket << "\n";
			}
		}

	private:
		void WriteHeader()
		{
			m_csv << "captureEpochMs,clientInstanceId,userId,sessionId,entity,protocol,"
				<< "rtt_ms,jitter_ms,packetLoss,recvThroughput_kbps,sendThroughput_kbps,bandwidthMbps,"
				<< "txPacketsPerSec,rxPacketsPerSec,txBytesPerSec,rxBytesPerSec,"
				<< "firstSendSuccessPct,rtxHitPct,rtxRecoveryPct,avgRtxPerHitPacket,"
				<< "goodputPct,fragEfficiencyPct,ackPiggybackHitPct,outOfOrderPct,duplicatePct,"
				<< "pendingReliableNow,pendingReliablePeek,maxRtxPerPacket,rtxTimeoutPct,rtxGiveupPct\n";
		}

		void WriteRow(const SessionMetricsRow& row)
		{
			m_rows.push_back(row);

			m_csv
				<< row.captureEpochMs << ","
				<< row.clientInstanceId << ","
				<< row.userId << ","
				<< row.sessionId << ","
				<< row.entity << ","
				<< row.protocol << ","
				<< row.rtt_ms << ","
				<< row.jitter_ms << ","
				<< row.packetLoss << ","
				<< row.recvThroughput_kbps << ","
				<< row.sendThroughput_kbps << ","
				<< row.bandwidthMbps << ","
				<< row.txPacketsPerSec << ","
				<< row.rxPacketsPerSec << ","
				<< row.txBytesPerSec << ","
				<< row.rxBytesPerSec << ","
				<< row.firstSendSuccessPct << ","
				<< row.rtxHitPct << ","
				<< row.rtxRecoveryPct << ","
				<< row.avgRtxPerHitPacket << ","
				<< row.goodputPct << ","
				<< row.fragEfficiencyPct << ","
				<< row.ackPiggybackHitPct << ","
				<< row.outOfOrderPct << ","
				<< row.duplicatePct << ","
				<< row.pendingReliableNow << ","
				<< row.pendingReliablePeek << ","
				<< row.maxRtxPerPacket << ","
				<< row.rtxTimeoutPct << ","
				<< row.rtxGiveupPct << "\n";
		}

	private:
		std::filesystem::path m_dir;
		std::filesystem::path m_csvPath;
		std::filesystem::path m_summaryPath;
		std::ofstream m_csv;
		std::vector<SessionMetricsRow> m_rows;
	};
}

static void Run(const TestConfig& config)
{
    const uint32 windowCount = std::min<uint32>(4, config.numUsers);

    auto& renderer = Renderer::Instance();
    renderer.Init(static_cast<int32>(windowCount));

    //const std::string mapGltfPath = "C://Users//akxotjr//GameWorkSpace//Jam//TestApp//Contents//ThirdPersonMap_L.glb";
    //if (!Renderer::Instance().LoadGLTFScene(mapGltfPath))
    //    throw std::runtime_error("failed to load level map");


    std::vector<std::unique_ptr<ClientInstance>> clients;

    uint32 instanceId = 0;
    uint64 userId = 1000;

    // User 클라이언트 생성 및 연결 (렌더링 포함)
    for (uint32 i = 0; i < config.numUsers; ++i, ++instanceId, ++userId)
    {
        auto client = std::make_unique<UserInstance>(instanceId, userId);

        if (client->Connect(config.serverIp, config.tcpPort, config.udpPort))
        {
            if (i < windowCount)
                client->SetWindowIndex(i);
            else
                client->SetWindowIndex(MAX_WINDOWS); // 렌더 비활성

            clients.push_back(std::move(client));
        }
        else
        {
            JAMNET_LOG_ERROR("Failed to connect client #{}", i);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 연결 간격 (서버 부하 분산)
    }

    // Bot 클라이언트 생성 및 연결 (렌더링 없음)
    for (uint32 i = 0; i < config.numBots; ++i, ++instanceId, ++userId)
    {
        auto client = std::make_unique<BotInstance>(instanceId, userId);

        if (client->Connect(config.serverIp, config.tcpPort, config.udpPort))
        {
            client->SetWindowIndex(MAX_WINDOWS);
            clients.push_back(std::move(client));
        }
        else
        {
            JAMNET_LOG_ERROR("Failed to connect bot #{}", i);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::this_thread::sleep_for(1s);

    for (auto& client : clients)
        client->SpawnPlayer();

    constexpr double targetFPS  = 120.0;
    const     auto   targetSpan = std::chrono::nanoseconds((int64_t)std::llround(1e9 / targetFPS));
    constexpr auto   sleepGuard = std::chrono::microseconds(2000);

    ScopedTimerResolution timerResGuard{};

	SessionCsvReporter reporter{};
	auto nextDump = std::chrono::steady_clock::now() + 1s;

    while (!renderer.ShouldClose())
    {
        const auto frameStart = std::chrono::steady_clock::now();

        MAIN_EXEC.PumpOnce();

        for (auto& client : clients)
        {
            client->Update(static_cast<float>(1.0 / targetFPS));
        }

        for (auto& client : clients)
        {
            client->Render();
        }

		const auto now = std::chrono::steady_clock::now();
		if (now >= nextDump)
		{
			reporter.DumpOnce(clients);
			nextDump += 1s;
		}

        double reqMs=0, actMs=0, spinMs=0;
        PreciseFrameSleep(frameStart, targetSpan, sleepGuard, reqMs, actMs, spinMs);
    }

    clients.clear();
    renderer.Shutdown();
}




int main()
{
    // -- test running -- 

    this_thread::sleep_for(0.5s);

    RuntimeConfig runtimeConfig{};
    runtimeConfig.geConfig.autoTune     = true;
    runtimeConfig.geConfig.layoutCfg = { .mode = jam::Balance, .reservedThreads = 1, .profile = jam::CoreProfileClient };

    JamNetRuntime runtime(runtimeConfig);

    PHYSICS_CORE_INIT();
    PHYSICS_PREFAB_REGISTRY.Init("C://Users//akxotjr//GameWorkSpace//Jam//TestApp//Contents//test_asset.json");


    TestConfig testConfig{};

    std::cout << "Number of users: ";
    std::cin >> testConfig.numUsers;

    std::cout << "Number of bots: ";
    std::cin >> testConfig.numBots;

    Run(testConfig);


    return 0;
}
