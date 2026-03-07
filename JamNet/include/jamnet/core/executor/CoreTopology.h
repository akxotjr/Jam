#pragma once

namespace jam
{
    struct CoreLayout
    {
        uint32  shards  = 1;
        uint32  offload = 1;
        uint32  spare   = 0;

        bool IsValid() const
        {
            return shards <= 0 || offload <= 0;
        }
    };

    enum eAutoCoreLayoutMode : uint8
    {
	    IO_HEAVY,
        CPU_HEAVY,
        BALANCE
    };

    enum eCoreUsageProfile : uint8
    {
        CORE_PROFILE_SERVER,   // 서버: 코어를 최대한 사용
        CORE_PROFILE_CLIENT    // 클라이언트: 여유 남겨두고 절반 정도만 사용
    };

    struct AutoCoreLayoutConfig
    {
        eAutoCoreLayoutMode mode             = BALANCE;
        bool                is_smt           = true;           // 하이퍼스레딩(논리>물리) 여부
        uint32              logical_cores    = std::thread::hardware_concurrency();      
        uint32              physical_cores   = 0;
        uint32              numa_nodes       = 1;      
        uint32              reserved_threads = 1;
        eCoreUsageProfile   profile          = CORE_PROFILE_SERVER;
        float               usage_scale      = 0.f;
    };

    inline CoreLayout AutoLayout(const AutoCoreLayoutConfig& cfg)
    {
        auto ceil_div = [](uint32 a, uint32 b) { return (a + b - 1) / b; };

        const uint32 logical = cfg.logical_cores ? cfg.logical_cores : 1;
        uint32 phys = cfg.physical_cores;
        if (phys == 0)
            phys = cfg.is_smt ? std::max<uint32>(1u, logical / 2u) : logical;

        // ---- 1) 메인 스레드용으로 예약하는 코어 수 반영 ----
        // reserved 만큼은 executor 쓰레드로 쓰지 않음.
        const uint32 reserved = std::min<uint32>(phys, cfg.reserved_threads);
        const uint32 rawBudget = std::max<uint32>(1u, phys - reserved);

        // ---- 2) 프로파일에 따라 budget 스케일링 ----
        //  - 서버: 남은 코어(rawBudget)를 전부 budget으로 사용
        //  - 클라: rawBudget 의 절반 정도만 사용해서 여유 남김

        float usageScale = cfg.usage_scale;
        if (usageScale == 0.f)
        {
            usageScale = cfg.profile == CORE_PROFILE_SERVER ? 1.0f : 0.5f;
        }

        uint32 budget = rawBudget;
        if (cfg.profile == CORE_PROFILE_CLIENT)
        {
            // 최소 1개는 보장
            budget = std::max<uint32>(1u, rawBudget * usageScale);
        }

        // 샤드 계산은 "예산" 기준(budget)으로 → 과잉 스레딩 방지
        CoreLayout L{};

        switch (cfg.mode)
        {
        case IO_HEAVY: // 대략 25%를 IO로, 하한 2개
        {
            L.offload = std::clamp<uint32>(ceil_div(budget, 4), 2, (budget > 1) ? budget - 1 : 1);
            L.spare = (budget >= 6) ? 1 : 0;
            break;
        }

        case CPU_HEAVY: // IO 최소, spare 은 0 
        {
            L.offload = 1;
            L.spare = 0;
            break;
        }

        case BALANCE: // 대략 12.5% 를 IO, spare 1개 (코어 여유가 있을때)
        default:
        {
            L.offload = std::clamp<uint32>(ceil_div(budget, 8), 1, (budget > 1) ? budget - 1 : 1);
            L.spare = (budget >= 8) ? 1 : 0;
            break;
        }
        }

        // 샤드 = budget - overhead (최소 1 보장)
        const uint32 overhead = L.offload + L.spare;
        L.shards = (budget > overhead) ? (budget - overhead) : 1u;

        // 총합이 "예약되지 않은 논리 코어 수"를 넘지 않도록 안전 클램프
        //  - 논리 코어 전체 중 reserved 개수는 이미 메인/렌더 등이 사용 중이라고 가정.
        auto total = [&]() { return L.shards + L.offload + L.spare; };
        const uint32 logicalCap = (logical > reserved) ? (logical - reserved) : 1u;
        if (total() > logicalCap)
        {
            uint32 extra = total() - logicalCap;
            auto trim = [&](uint32& x)
                {
                    uint32 cut = std::min<uint32>(x, extra);
                    x -= cut;
                    extra -= cut;
                };

            // 우선순위: spare → offload → shards
            trim(L.spare);
            if (extra) trim(L.offload);
            if (extra && L.shards > 1) trim(L.shards);
            if (L.shards == 0) L.shards = 1;
        }

        // NUMA 힌트: 샤드를 노드 수 배수로 맞추면 배치/핀닝이 쉬움
        const uint32 nodes = cfg.numa_nodes ? cfg.numa_nodes : 1u;
        if (nodes > 1 && L.shards >= nodes)
        {
            uint32 rem = L.shards % nodes;
            if (rem != 0)
            {
                uint32 canGrow = (total() < logicalCap) ? (logicalCap - total()) : 0u;
                uint32 add = nodes - rem;
                if (add <= canGrow)
                {
                    L.shards += add;        // 여유가 있으면 올림(분배 깔끔)
                }
                else
                {
                    L.shards -= rem;        // 아니면 내림
                    if (L.shards == 0) L.shards = nodes; // 안전 하한
                }
            }
        }

        return L;
    }
}

