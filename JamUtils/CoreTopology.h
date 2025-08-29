#pragma once

namespace jam::utils::sys
{
    struct CoreLayout
    {
        uint32  shards;
        uint32  io;
        uint32  timers;
        uint32  spare;
    };

    struct AutoLayoutConfig
    {
        bool    io_heavy = false;
        bool    cpu_heavy = false;
        bool    is_smt = false;           // 하이퍼스레딩(논리>물리) 여부
        uint32  logical_cores = 0;        // std::thread::hardware_concurrency()
        uint32  physical_cores = 0;       // 모르면 0
        uint32  numa_nodes = 0;           // 모르면 0 → 1로 처리
    };

    inline CoreLayout AutoLayout(const AutoLayoutConfig& cfg)
    {
        auto clampu = [](uint32 v, uint32 lo, uint32 hi) { return v < lo ? lo : (v > hi ? hi : v); };
        auto ceil_div = [](uint32 a, uint32 b) { return (a + b - 1) / b; };

        // 논리/물리 코어 파악
        const uint32 logical = cfg.logical_cores ? cfg.logical_cores : 1u;
        uint32 phys = cfg.physical_cores;
        if (phys == 0) 
        {
            // 물리 코어 정보가 없으면:
            // - SMT이면 논리의 절반 추정 (최소 1)
            // - 아니면 논리=물리로 간주
            phys = cfg.is_smt ? max(1u, logical / 2u) : logical;
        }

        // 샤드 계산은 물리 코어 기준(budget)으로 → 과잉 스레딩 방지
        const uint32 budget = max(1u, phys);

        CoreLayout L{}; // 0 초기화

        // 초소형 머신(코어 1~3) 보호
        if (budget == 1) { L.shards = 1; return L; }
        if (budget == 2) { L.io = 1; L.timers = 0; L.spare = 0; L.shards = 1; return L; }
        if (budget == 3) { L.io = 1; L.timers = 1; L.spare = 0; L.shards = 1; return L; }

        // 기본 규칙(확장형) — 비율 기반으로 스케일
        if (cfg.io_heavy) 
        {
            // 대략 25%를 IO로, 하한 2개
            L.io = clampu(ceil_div(budget, 4), 2u, budget - 1);
            L.timers = 1;
            L.spare = (budget >= 6) ? 1u : 0u;
        }
        else if (cfg.cpu_heavy) 
        {
            // CPU 헤비는 IO 최소, spare은 0으로 당김
            L.io = 1;
            L.timers = 1;
            L.spare = 0;
        }
        else 
        {
            // balanced: IO ~12.5%, timers 1, spare 1(코어 여유가 있을 때)
            L.io = clampu(ceil_div(budget, 8), 1u, budget - 1);
            L.timers = 1;
            L.spare = (budget >= 8) ? 1u : 0u;
        }

        // 샤드 = budget - overhead (최소 1 보장)
        const uint32 overhead = L.io + L.timers + L.spare;
        L.shards = (budget > overhead) ? (budget - overhead) : 1u;

        // 총합이 논리 코어 수를 넘지 않도록 안전 클램프
        // (논리 코어는 OS 스케줄러 기준의 상한)
        auto total = [&]() { return L.shards + L.io + L.timers + L.spare; };
        if (total() > logical) 
        {
            uint32 extra = total() - logical;
            auto trim = [&](uint32& x) { uint32 cut = min(x, extra); x -= cut; extra -= cut; };

            // 우선순위: spare → io → timers → shards
            trim(L.spare);
            if (extra) trim(L.io);
            if (extra) trim(L.timers);
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
                uint32 canGrow = (total() < logical) ? (logical - total()) : 0u;
                uint32 add = nodes - rem;
                if (add <= canGrow) 
                {
                    L.shards += add;        // 여유가 있으면 올림(분배 깔끔)
                }
                else {
                    L.shards -= rem;        // 아니면 내림
                    if (L.shards == 0) L.shards = nodes; // 안전 하한
                }
            }
        }

        return L;
    }


    inline CoreLayout ResolveCoreLayout(const CoreLayout& manual, const AutoLayoutConfig& acfg)
    {
        const uint32 logical = acfg.logical_cores ? acfg.logical_cores : 1u;

        // 1) 자동 레이아웃 계산
        CoreLayout A = AutoLayout(acfg);

        // 2) 수동값 우선, 0인 필드만 자동값으로 채움
        CoreLayout L{};
        L.shards = (manual.shards == 0) ? A.shards : manual.shards;
        L.io = (manual.io == 0) ? A.io : manual.io;
        L.timers = (manual.timers == 0) ? A.timers : manual.timers;
        L.spare = (manual.spare == 0) ? A.spare : manual.spare;

        // 3) 최소 보장
        if (L.shards == 0) L.shards = 1;          // 반드시 1개 이상
        // (io/timers/spare는 0도 허용)

        // 4) 총합이 논리 코어 초과 시 클램프 (우선순위: spare → io → timers → shards)
        auto total = [&]() { return L.shards + L.io + L.timers + L.spare; };
        if (total() > logical) 
        {
            uint32 extra = total() - logical;

            auto trim = [&](uint32& x) {
					uint32 cut = (x > extra) ? extra : x;
					x -= cut; extra -= cut;
                };

            trim(L.spare);
            if (extra) trim(L.io);
            if (extra) trim(L.timers);
            if (extra && L.shards > 1) trim(L.shards);
            if (L.shards == 0) L.shards = 1;
        }

        // 5) (선택) NUMA 배치가 쉬우도록 shards를 노드 수 배수로 맞추기
        const uint32 nodes = acfg.numa_nodes ? acfg.numa_nodes : 1u;
        if (nodes > 1 && L.shards >= nodes) 
        {
            uint32 rem = L.shards % nodes;
            if (rem != 0) {
                // 남는 여유가 있으면 올림, 없으면 내림
                const uint32 logicalCap = logical; // 총합 상한
                uint32 canGrow = (total() < logicalCap) ? (logicalCap - total()) : 0u;
                uint32 add = nodes - rem;
                if (add <= canGrow) 
                {
                    L.shards += add;
                }
                else 
                {
                    L.shards -= rem;
                    if (L.shards == 0) L.shards = nodes; // 안전 하한
                }
            }
        }

        return L;
    }
}

