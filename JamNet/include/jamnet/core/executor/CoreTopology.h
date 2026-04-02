#pragma once

namespace jam
{
    struct CoreLayout
    {
        int32  shards  = 1;
        int32  offload = 1;
        int32  spare   = 0;

        bool IsValid() const
        {
            return shards <= 0 || offload <= 0;
        }
    };

    enum eAutoCoreLayoutMode : uint8
    {
	    IO_Heavy,
        CPU_Heavy,
        Balance
    };

    enum eCoreUsageProfile : uint8
    {
        CoreProfileServer,   // 서버: 코어를 최대한 사용
        CoreProfileClient    // 클라이언트: 여유 남겨두고 절반 정도만 사용
    };

    struct AutoCoreLayoutConfig
    {
        eAutoCoreLayoutMode     mode             = Balance;
        bool                    is_smt           = true;           // 하이퍼스레딩(논리>물리) 여부
        uint32                  logical_cores    = std::thread::hardware_concurrency();      
        uint32                  physical_cores   = 0;
        uint32                  numa_nodes       = 1;      
        uint32                  reserved_threads = 1;
        eCoreUsageProfile       profile          = CoreProfileServer;
        float                   usage_scale      = 0.f;
    };

    inline CoreLayout AutoLayout(const AutoCoreLayoutConfig& cfg)
    {
        auto ceil_div = [](uint32 a, uint32 b) { return (a + b - 1) / b; };

        const uint32 logical = cfg.logical_cores ? cfg.logical_cores : 1;
        uint32 phys = cfg.physical_cores;
        if (phys == 0)
            phys = cfg.is_smt ? std::max<uint32>(1u, logical / 2u) : logical;

        // reserved 만큼은 executor 쓰레드로 쓰지 않음.
        const uint32 reserved  = std::min<uint32>(phys, cfg.reserved_threads);
        const uint32 rawBudget = std::max<uint32>(1u, phys - reserved);


        // 서버: 남은 코어(rawBudget)를 전부 budget으로 사용
        // 클라: rawBudget 의 절반 정도만 사용해서 여유 남김

        float usageScale = cfg.usage_scale;
        if (usageScale == 0.f)
        {
            usageScale = cfg.profile == CoreProfileServer ? 1.0f : 0.5f;
        }

        uint32 budget = rawBudget;
        if (cfg.profile == CoreProfileClient)
        {
            // 최소 1개는 보장
            budget = std::max<uint32>(1u, rawBudget * usageScale);
        }

        CoreLayout layout{};

        switch (cfg.mode)
        {
        case IO_Heavy: // 대략 25%를 IO로, 하한 2개
        {
            layout.offload = std::clamp<uint32>(ceil_div(budget, 4), 2, (budget > 1) ? budget - 1 : 1);
            layout.spare   = (budget >= 6) ? 1 : 0;
            break;
        }

        case CPU_Heavy: // IO 최소, spare 은 0 
        {
            layout.offload = 1;
            layout.spare   = 0;
            break;
        }

        case Balance: // 대략 12.5% 를 IO, spare 1개 (코어 여유가 있을때)
        default:
        {
            layout.offload = std::clamp<uint32>(ceil_div(budget, 8), 1, (budget > 1) ? budget - 1 : 1);
            layout.spare   = (budget >= 8) ? 1 : 0;
            break;
        }
        }

        // 샤드 = budget - overhead (최소 1 보장)
        const uint32 overhead = layout.offload + layout.spare;
        layout.shards = (budget > overhead) ? (budget - overhead) : 1u;

        // 논리 코어 전체 중 reserved 개수는 이미 메인/렌더 등이 사용 중이라고 가정.
        auto total = [&]() { return layout.shards + layout.offload + layout.spare; };
        const uint32 logicalCap = (logical > reserved) ? (logical - reserved) : 1u;
        if (total() > logicalCap)
        {
            uint32 extra = total() - logicalCap;
            auto trim = [&](int32& x)
                {
                    int32 cut = std::min<int32>(x, extra);
                    x     -= cut;
                    extra -= cut;
                };

            trim(layout.spare);
            if (extra) trim(layout.offload);
            if (extra && layout.shards > 1) trim(layout.shards);
            if (layout.shards == 0) layout.shards = 1;
        }

        const uint32 nodes = cfg.numa_nodes ? cfg.numa_nodes : 1u;
        if (nodes > 1 && layout.shards >= nodes)
        {
            uint32 rem = layout.shards % nodes;
            if (rem != 0)
            {
                uint32 canGrow = (total() < logicalCap) ? (logicalCap - total()) : 0u;
                uint32 add = nodes - rem;
                if (add <= canGrow)
                {
                    layout.shards += add;      
                }
                else
                {
                    layout.shards -= rem;    
                    if (layout.shards == 0) layout.shards = nodes; 
                }
            }
        }

        return layout;
    }
}

