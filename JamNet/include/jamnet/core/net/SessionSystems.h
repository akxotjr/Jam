#pragma once

#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/SessionComponents.h"


namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{
	// ============================================================
	// Bootstrap Systems
	// ============================================================


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
		ShardLocal&                 L;              // ECS WORLD + Scheduler
		entt::entity                e;              // 대상 세션 엔티티
		PacketHeaderView            view;           // 패킷 분석 결과
		Packet                      packet;         // 원본 버퍼 (수명 관리)
		Timestamp                   now;			// 현재 타임스탬프
		Timestamp                   ingressTime;	// IOCP ingress 시각(가능한 wire 근접)

		bool                        shouldDrop          = false;
		bool                        isReassembling      = false;
		bool                        needsReordering     = false;
		bool                        flushOrderedPending = false;
		uint16                      orderedSpan         = 1;
	};

	// Pipeline Stage Functions

   bool IncomingSequencingProcess(RecvContext& ctx);
   bool IncomingOrderingProcess(RecvContext& ctx);
   bool IncomingReliabilityProcess(RecvContext& ctx);
   bool IncomingFragmentationProcess(RecvContext& ctx);
   bool IncomingNetstatProcess(RecvContext& ctx);
   
   bool HandlePostBindSystemPacket(RecvContext& ctx);
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
		PacketHeaderView                    header;
		Packet                              packet;
		Timestamp                           now_ns          = Timestamp::Zero();
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
	void SystemSessionTimeout(ShardLocal& L, Timestamp now, Duration dt);

	/// Keepalive 시스템
	void SystemSessionKeepalive(ShardLocal& L, Timestamp now, Duration dt);

	/// RPC 타임아웃 시스템
	void SystemRpcTimeout(ShardLocal& L, Timestamp now, Duration dt);

	/// 전송 대기열 플러시 시스템
	void SystemTransportFlush(ShardLocal& L, Timestamp now, Duration dt);

	/// 재전송 시스템
	void SystemRetransmit(ShardLocal& L, Timestamp now_ns, Duration dt);

	/// Fragment 타임아웃 정리 시스템
	void SystemFragmentCleanup(ShardLocal& L, Timestamp now, Duration dt);

	/// 통계 누적 시스템
	void SystemNetworkStats(ShardLocal& L, Timestamp now, Duration dt);


	// ============================================================
	// Helper Functions
	// ============================================================


	bool SendPacketToSession(entt::entity e, Packet packet);

	/// 세션 수신 처리
	void ProcessReceivedPacket(entt::entity e, Packet packet, Timestamp ingressRecvTime = Timestamp::Zero());


	// ============================================================
	// Domain Registration Helpers
	// ============================================================

	/// 네트워크 도메인 시스템 등록 (NETWORK domain)
	void RegisterNetworkDomain(ShardLocal& L, Duration tickPeriod = 1_ms);

} // namespace jam::net
