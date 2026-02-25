#pragma once


namespace jam::net
{
    // ============================================================
   // NETWORK 도메인 초기화
   // ============================================================

   /// NETWORK 도메인 시스템 그룹 설정
    inline void BootstrapNetworkDomain(ShardLocal& L)
    {
        auto& R = L.world;

        // ============================================================
        // 1. ECS Context 초기화
        // ============================================================

        if (!R.ctx().contains<SessionDataPools>())
        {
            R.ctx().emplace<SessionDataPools>();
        }

        if (!R.ctx().contains<RpcHandlersRegistry>())
        {
            R.ctx().emplace<RpcHandlersRegistry>();
        }

        // ============================================================
        // 2. SystemGroup 설정
        // ============================================================

        auto& networkGroup = L.domainGroups[E2U(eDomainType::NETWORK)];

        networkGroup.domain = eDomainType::NETWORK;
        networkGroup.tickPeriod_ns = 16'000'000_ns;  // 16ms (60 tick/sec)

        // ============================================================
        // 3. Tick 시스템 등록
        // ============================================================

        networkGroup.systems.push_back(
            [](ShardLocal& L, uint64 now_ns, uint64 dt_ns)
            {
                SessionTickContext ctx{ L, now_ns, dt_ns };
                ProcessSessionTickSystems(ctx);
            }
        );

        // ============================================================
        // 4. 엔티티 필터 설정 (세션만 처리)
        // ============================================================

        networkGroup.entityFilter = [](entt::registry& R, entt::entity e)
            {
                // CompSessionInfo를 가진 엔티티만 세션으로 간주
                return R.all_of<CompSessionInfo>(e);
            };
    }


    // ============================================================
    // 세션 생성 헬퍼
    // ============================================================

    /// 새 세션 엔티티 생성 및 컴포넌트 초기화
    inline entt::entity CreateSessionEntity(
        ShardLocal& L,
        uint64 sessionId,
        const SOCKADDR_IN& addr,
        class ISession* owner)
    {
        auto& R = L.world;
        auto e = R.create();

        // 기본 정보
        auto& info = R.emplace<CompSessionInfo>(e);
        info.sessionId = sessionId;
        info.createdTime_ns = NOW_NS();
        info.lastActiveTime_ns = info.createdTime_ns;
        info.state = CompSessionInfo::State::CONNECTING;

        // 엔드포인트
        auto& ep = R.emplace<CompEndpoint>(e);
        ep.addr = addr;
        ep.owner = owner;

        // 채널
        R.emplace<CompChannel>(e);

        // 신뢰성
        R.emplace<CompReliability>(e);

        // 분할
        R.emplace<CompFragment>(e);

        // RPC
        R.emplace<CompRpc>(e);

        // 통계
        R.emplace<CompNetstat>(e);

        // 혼잡 제어
        R.emplace<CompCongestion>(e);

        return e;
    }


    // ============================================================
    // 세션 파괴 헬퍼
    // ============================================================

    inline void DestroySessionEntity(ShardLocal& L, entt::entity e)
    {
        auto& R = L.world;

        if (!R.valid(e))
            return;

        // 정리 작업 (필요시)
        // - RPC 타임아웃 처리
        // - 재전송 큐 비우기
        // - 통계 최종 기록 등

        R.destroy(e);
    }
}