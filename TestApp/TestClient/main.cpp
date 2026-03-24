#include "pch.h"

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")


#include "ClientInstance.h"
#include "Renderer.h"
#include "jampx/PhysicsCore.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/prefab/PrefabCooker.h"
#include "jampx/prefab/PrefabDocument.h"


using namespace jam::net;


struct TestConfig
{
    uint32 numClients   = 1;
    string serverIp     = "127.0.0.1";
    uint16 tcpPort      = 7777;
    uint16 udpPort      = 8888;
};

// 고해상도 슬립 보조: 남은 시간이 임계값 이상이면 sleep_for 후 마무리는 얕은 스핀/양보
static inline void PreciseFrameSleep(std::chrono::steady_clock::time_point frameStart,
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



static void Run(const TestConfig& config)
{
    const uint32 windowCount = std::min<uint32>(4, config.numClients);
    const uint32 clientCount = config.numClients;

    auto& renderer = Renderer::Instance();
    renderer.Init(static_cast<int32>(windowCount));

    //const std::string mapGltfPath = "C://Users//akxotjr//GameWorkSpace//Jam//TestApp//Contents//ThirdPersonMap_L.glb";
    //if (!Renderer::Instance().LoadGLTFScene(mapGltfPath))
    //    throw std::runtime_error("failed to load level map");


    std::vector<std::unique_ptr<ClientInstance>> clients;

    // 모든 클라이언트 생성 및 연결
    for (uint32 i = 0; i < clientCount; ++i)
    {
        auto client = std::make_unique<ClientInstance>(i, i + 1000);

        if (client->Connect(config.serverIp, config.tcpPort, config.udpPort))
        {
            clients.push_back(std::move(client));
            clients[i]->SetWindowIndex(i);
        }
        else
        {
            JAMNET_LOG_ERROR("Failed to connect client #{}", i);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 연결 간격 (서버 부하 분산)
    }

    std::this_thread::sleep_for(1s);

    for (uint32 i = 0; i < clientCount; ++i)
    {
        if (i < windowCount)
        {
            clients[i]->SpawnActor();
        }
    }

    constexpr double targetFPS  = 120.0;
    const     auto   targetSpan = std::chrono::nanoseconds((int64_t)std::llround(1e9 / targetFPS));
    constexpr auto   sleepGuard = std::chrono::microseconds(2000);

    ScopedTimerResolution timerResGuard{};

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
    runtimeConfig.geConfig.layoutCfg = { .mode = jam::BALANCE, .reserved_threads = 1, .profile = jam::CORE_PROFILE_CLIENT };

    JamNetRuntime runtime(runtimeConfig);

    PHYSICS_CORE_INIT();
    PHYSICS_PREFAB_REGISTRY.Init("C://Users//akxotjr//GameWorkSpace//Jam//TestApp//Contents//test_asset.json");



    TestConfig testConfig{};

    std::cout << "Number of clients: ";
    std::cin >> testConfig.numClients;

    Run(testConfig);


    return 0;
}