#pragma once

#include "jamnet/core/net/SessionComponents.h"


namespace jam::net
{
    // ============================================================
    // Bootstrap Systems
    // ============================================================

    struct NetworkSystemBootstrappedTag {};

    /// 네트워크 시스템 초기화
    void BootstrapNetworkSystems(ShardLocal& L);

    /// 세션 컴포넌트 초기화
    void BootstrapSessionEntity(ShardLocal& L, entt::entity e, Session* session);


    // ============================================================
	//  Packet Incoming Pipeline
	// ============================================================
    
	// Receive Packet Processing Context

	/// 패킷 처리에 필요한 모든 컨텍스트를 담는 구조체
    /// - 이벤트 객체 생성 대신 이 구조체를 재사용하여 메모리 압력 감소
    struct RecvContext
    {
        ShardLocal&                 L;          // ECS WORLD + Scheduler
        entt::entity                e;          // 대상 세션 엔티티
        PacketView                  view;       // 패킷 분석 결과
        std::shared_ptr<RecvBuffer> buf;        // 원본 버퍼 (수명 관리)
        uint64                      now_ns;     // 현재 타임스탬프

        bool                        bShouldDrop      = false;
        bool                        bIsReassembling  = false;
        bool                        bNeedsReordering = false;
    };

    // Pipeline Stage Functions

   bool IncomingSequencingProcess(RecvContext& ctx);
   bool IncomingOrderingProcess(RecvContext& ctx);
   bool IncomingReliabilityProcess(RecvContext& ctx);
   bool IncomingFragmentationProcess(RecvContext& ctx);
   bool IncomingNetstatProcess(RecvContext& ctx);
   
   void HandleSystemPacket(RecvContext& ctx);
   void HandleAckPacket(RecvContext& ctx);


	// Main Pipeline Orchestrator

	/// **메인 수신 파이프라인**
	/// - 모든 수신 패킷은 이 함수를 통해 순차 처리
	/// - 이벤트 디스패처 대신 직접 함수 호출 체인 사용
	///
	/// @param ctx 패킷 처리 컨텍스트
	void PipelineIncomingPacket(RecvContext& ctx);


    // ============================================================
    //  Packet Outgoing Pipeline
    // ============================================================

    // Send Packet Processing Context

    struct SendContext
    {
        ShardLocal&                         L;
        entt::entity                        e;
        PacketView                          view;
        std::shared_ptr<SendBuffer>         buf;
        TxPriority                          priority        = TxPriority::NORMAL;
        uint64                              now_ns          = 0_ns;
        bool                                bIsFragmentized = false;
    };

    bool OutgoingFragmentationProcess(SendContext& ctx);
    bool OutgoingSequencingProcess(SendContext& ctx);

    /// **메인 송신 파이프라인**
	/// - 모든 송신 패킷은 이 함수를 통해 순차 처리
	void PipelineOutgoingPacket(SendContext& ctx);


    // ============================================================
    //  Tick Systems (주기적 실행)
    // ============================================================

    /// 타임아웃 체크 시스템
    void SystemSessionTimeout(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

    /// Keepalive 시스템
    void SystemSessionKeepalive(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

    /// RPC 타임아웃 시스템
    void SystemRpcTimeout(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

    /// 전송 대기열 플러시 시스템
    void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

    /// 재전송 시스템
    void SystemRetransmit(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

    /// Fragment 타임아웃 정리 시스템
    void SystemFragmentCleanup(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

    /// 핸드셰이크 타임아웃 시스템
    void SystemHandshakeTimeout(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

    /// 통계 업데이트 시스템
    void SystemNetworkStats(ShardLocal& L, uint64 now_ns, uint64 dt_ns);


    // ============================================================
    // Helper Functions
    // ============================================================

    void ConnectHandshake(entt::entity e);
    void DisconnectHandshake(entt::entity e);

    void SendPacketToSession(entt::entity e, const std::shared_ptr<SendBuffer>& buf);

    /// 세션 수신 처리
    void ProcessReceivedPacket(entt::entity e, const std::shared_ptr<RecvBuffer>& buf);


    // ============================================================
    // Domain Registration Helpers
    // ============================================================

    /// 네트워크 도메인 시스템 등록 (NETWORK domain)
    void RegisterNetworkDomain(ShardLocal& L, uint64 tickPeriod_ns = 1'000'000_ns);

} // namespace jam::net