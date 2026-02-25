#include "pch.h"

#include <conio.h>


// ---- High-resolution frame pacing & stats ----
#include <atomic>
#include <cmath>


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

    const std::string mapGltfPath = (std::filesystem::current_path() / "Contents" / "ThirdPersonMap11.glb").string();
    if (!Renderer::Instance().LoadGLTFScene(mapGltfPath))
        throw std::runtime_error("failed to load level map");


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

    constexpr double targetFPS = 120.0;
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




static void WriteTestPrefabAndLevel()
{
    namespace fs = std::filesystem;
    using namespace jam::px::prefab;

    fs::path contents = fs::current_path() / "Contents";
    std::error_code ec;
    fs::create_directories(contents, ec);

    // ----------------------------
    // Prefab
    // ----------------------------
    fs::path prefabPath = contents / "test_prefab.json";

    PrefabDocument prefabDoc{};
    prefabDoc.path = prefabPath;

    {
        PrefabEditor ed(prefabDoc);

        ed.Set(json::json_pointer(pVersion), 1);
        ed.Set(json::json_pointer(pTemplates), json::array());

        constexpr size_t characterTemplateIndex = 0;
        constexpr size_t mapTemplateIndex = 1;

        // ---- Character template ----
        ed.Set(MakeTemplateFieldPtr(characterTemplateIndex, kName), "Character");
        ed.Set(MakeTemplateFieldPtr(characterTemplateIndex, kKind), vKindCharacter);
        ed.Set(MakeTemplateFieldPtr(characterTemplateIndex, kSpawnPolicy), vSpawnBoth);
        ed.Set(MakeTemplateFieldPtr(characterTemplateIndex, kAllowReplication), true);
        ed.Set(MakeTemplateFieldPtr(characterTemplateIndex, kShapes), json::array());

        // cct
        ed.Set(MakeCctFieldPtr(characterTemplateIndex, kCctRadius), 0.35);
        ed.Set(MakeCctFieldPtr(characterTemplateIndex, kCctHeight), 1.8);
        ed.Set(MakeCctFieldPtr(characterTemplateIndex, kAllowCrouch), true);

        ed.EnsureObject(MakeCctFieldPtr(characterTemplateIndex, kMaterial));
        ed.Set(json::json_pointer(MakeCctFieldPtr(characterTemplateIndex, kMaterial).to_string() + "/" + kStaticFriction), 0.5);
        ed.Set(json::json_pointer(MakeCctFieldPtr(characterTemplateIndex, kMaterial).to_string() + "/" + kDynamicFriction), 0.5);
        ed.Set(json::json_pointer(MakeCctFieldPtr(characterTemplateIndex, kMaterial).to_string() + "/" + kRestitution), 0.1);

        ed.EnsureObject(MakeCctMovementPtr(characterTemplateIndex));
        ed.Set(MakeCctMovementFieldPtr(characterTemplateIndex, kWalkSpeed), 5.0);
        ed.Set(MakeCctMovementFieldPtr(characterTemplateIndex, kSprintSpeed), 7.5);
        ed.Set(MakeCctMovementFieldPtr(characterTemplateIndex, kCrouchSpeed), 3.0);
        ed.Set(MakeCctMovementFieldPtr(characterTemplateIndex, kAccelGround), 35.0);
        ed.Set(MakeCctMovementFieldPtr(characterTemplateIndex, kGravity), 25.0);
        ed.Set(MakeCctMovementFieldPtr(characterTemplateIndex, kJumpSpeed), 7.0);

        // character shape[0]
        constexpr size_t characterShapeIndex = 0;
        ed.Set(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kType), vShapeCapsule);
        ed.Set(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kShapeFlag), vShapeFlagSimulation);

        ed.EnsureObject(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kLocalPose));
        ed.Set(json::json_pointer(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kLocalPose).to_string() + "/" + kP),
            json::array({ 0.0, 0.9, 0.0 }));
        ed.Set(json::json_pointer(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kLocalPose).to_string() + "/" + kQ),
            json::array({ 0.0, 0.0, 0.0, 1.0 }));

        ed.EnsureObject(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kMaterial));
        ed.Set(MakeShapeMaterialFieldPtr(characterTemplateIndex, characterShapeIndex, kStaticFriction), 0.5);
        ed.Set(MakeShapeMaterialFieldPtr(characterTemplateIndex, characterShapeIndex, kDynamicFriction), 0.5);
        ed.Set(MakeShapeMaterialFieldPtr(characterTemplateIndex, characterShapeIndex, kRestitution), 0.0);

        ed.EnsureObject(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kSimFilter));
        ed.Set(json::json_pointer(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kSimFilter).to_string() + "/" + kWord0), 1);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kSimFilter).to_string() + "/" + kWord1), 0);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kSimFilter).to_string() + "/" + kWord2), 0);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kSimFilter).to_string() + "/" + kWord3), 0);

        ed.EnsureObject(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kQryFilter));
        ed.Set(json::json_pointer(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kQryFilter).to_string() + "/" + kWord0), 0u);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kQryFilter).to_string() + "/" + kWord1), 0u);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kQryFilter).to_string() + "/" + kWord2), 0u);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kQryFilter).to_string() + "/" + kWord3), 0u);

        ed.Set(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kRadius), 0.35);
        ed.Set(MakeShapeFieldPtr(characterTemplateIndex, characterShapeIndex, kHalfHeight), 0.9);

        // ---- Map template ----
        ed.Set(MakeTemplateFieldPtr(mapTemplateIndex, kName), "Map");
        ed.Set(MakeTemplateFieldPtr(mapTemplateIndex, kKind), vKindStatic);
        ed.Set(MakeTemplateFieldPtr(mapTemplateIndex, kSpawnPolicy), vSpawnLevelOnly);
        ed.Set(MakeTemplateFieldPtr(mapTemplateIndex, kAllowReplication), false);
        ed.Set(MakeTemplateFieldPtr(mapTemplateIndex, kShapes), json::array());

        constexpr size_t mapShapeIndex = 0;
        ed.Set(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kType), vShapeTriangleMesh);
        ed.Set(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kShapeFlag), vShapeFlagSimulation);

        ed.EnsureObject(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kLocalPose));
        ed.Set(json::json_pointer(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kLocalPose).to_string() + "/" + kP),
            json::array({ 0.0, 0.0, 0.0 }));
        ed.Set(json::json_pointer(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kLocalPose).to_string() + "/" + kQ),
            json::array({ 0.0, 0.0, 0.0, 1.0 }));

        ed.EnsureObject(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kMaterial));
        ed.Set(MakeShapeMaterialFieldPtr(mapTemplateIndex, mapShapeIndex, kStaticFriction), 0.6);
        ed.Set(MakeShapeMaterialFieldPtr(mapTemplateIndex, mapShapeIndex, kDynamicFriction), 0.6);
        ed.Set(MakeShapeMaterialFieldPtr(mapTemplateIndex, mapShapeIndex, kRestitution), 0.0);

        ed.EnsureObject(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kSimFilter));
        ed.Set(json::json_pointer(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kSimFilter).to_string() + "/" + kWord0), 1);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kSimFilter).to_string() + "/" + kWord1), 0);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kSimFilter).to_string() + "/" + kWord2), 0);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kSimFilter).to_string() + "/" + kWord3), 0);

        ed.EnsureObject(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kQryFilter));
        ed.Set(json::json_pointer(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kQryFilter).to_string() + "/" + kWord0), 0u);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kQryFilter).to_string() + "/" + kWord1), 0u);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kQryFilter).to_string() + "/" + kWord2), 0u);
        ed.Set(json::json_pointer(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kQryFilter).to_string() + "/" + kWord3), 0u);

        ed.EnsureObject(MakeShapeFieldPtr(mapTemplateIndex, mapShapeIndex, kMesh));
        ed.Set(MakeShapeMeshFieldPtr(mapTemplateIndex, mapShapeIndex, kCooked), "Contents/ThridPersonMap.pxtri");
        ed.Set(MakeShapeMeshFieldPtr(mapTemplateIndex, mapShapeIndex, kMeshIndex), 0);
        ed.Set(MakeShapeMeshFieldPtr(mapTemplateIndex, mapShapeIndex, kPrimitiveIndex), 0);
    }

    try
    {
        SavePrefab(prefabDoc);
    }
    catch (const std::exception& e)
    {
        std::cerr << "SavePrefab failed: " << e.what() << std::endl;
    }

    // ----------------------------
    // Level
    // ----------------------------
    fs::path levelPath = contents / "test_level.json";

    PrefabLevelDocument levelDoc{};
    levelDoc.path = levelPath;

    {
        PrefabLevelEditor ed(levelDoc);

        ed.Set(json::json_pointer("/version"), 1);
        ed.Set(json::json_pointer("/instances"), json::array());

        constexpr size_t mapInstanceIndex = 0;
        constexpr size_t charInstanceIndex = 1;

        // map instance
        ed.Set(json::json_pointer("/instances/" + std::to_string(mapInstanceIndex) + "/template"), "Map");
        ed.EnsureObject(json::json_pointer("/instances/" + std::to_string(mapInstanceIndex) + "/pose"));
        ed.Set(json::json_pointer("/instances/" + std::to_string(mapInstanceIndex) + "/pose/p"), json::array({ 0.0, 0.0, 0.0 }));
        ed.Set(json::json_pointer("/instances/" + std::to_string(mapInstanceIndex) + "/pose/q"), json::array({ 0.0, 0.0, 0.0, 1.0 }));

        // character instance
        ed.Set(json::json_pointer("/instances/" + std::to_string(charInstanceIndex) + "/template"), "Character");
        ed.EnsureObject(json::json_pointer("/instances/" + std::to_string(charInstanceIndex) + "/pose"));
        ed.Set(json::json_pointer("/instances/" + std::to_string(charInstanceIndex) + "/pose/p"), json::array({ 2.0, 0.0, 2.0 }));
        ed.Set(json::json_pointer("/instances/" + std::to_string(charInstanceIndex) + "/pose/q"), json::array({ 0.0, 0.0, 0.0, 1.0 }));

        ed.EnsureObject(json::json_pointer("/instances/" + std::to_string(charInstanceIndex) + "/overrides"));
        ed.Set(json::json_pointer("/instances/" + std::to_string(charInstanceIndex) + "/overrides/linear_velocity"), json::array({ 0.0, 0.0, 0.0 }));
        ed.Set(json::json_pointer("/instances/" + std::to_string(charInstanceIndex) + "/overrides/angular_velocity"), json::array({ 0.0, 0.0, 0.0 }));
    }

    try
    {
        SavePrefabLevel(levelDoc);
    }
    catch (const std::exception& e)
    {
        std::cerr << "SavePrefabLevel failed: " << e.what() << '\n';
    }
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
    PHYSICS_PREFAB_REGISTRY.Init("C://Users//akxotjr//GameWorkSpace//Jam//TestApp//TestClient//Contents//test_prefab.json");

    TestConfig testConfig{};

    std::cout << "Number of clients: ";
    std::cin >> testConfig.numClients;

    Run(testConfig);

    
    // -- test write prefab and level --

    //WriteTestPrefabAndLevel();


    // -- test cooking --

    //PHYSICS_CORE_INIT();

    //px::prefab::PrefabCooker cooker;
    //cooker.CookTriangleMesh(
    //    "C://Users//akxotjr//GameWorkSpace//Jam//TestApp//TestClient//Contents//ThirdPersonMap11.glb",
    //    "C://Users//akxotjr//GameWorkSpace//Jam//TestApp//TestClient//Contents/ThirdPersonMap11.pxtri");

    return 0;
}