#pragma once
#include <bitset>

#include "jamnet/core/net/RecvBuffer.h"
#include "jamnet/core/net/NetworkProfile.h"


namespace jam::net
{
	class Session;


	// ============================================================
	// Configuration Constants
	// ============================================================

	constexpr uint8		MAX_RETRY						= 5;
	constexpr uint64	RETRANSMIT_INTERVAL_NS			= 40_ms;
	constexpr uint64	RETRANSMIT_TIMEOUT_NS			= 200_ms;
	constexpr uint32	ACK_TRACK_SIZE					= 1024;
	constexpr uint32	ACK_WINDOW_SIZE					= 32;
	constexpr uint64	DELAY_PIGGYBACK_ACK_TIMEOUT_NS  = 200_ms;
	constexpr uint64	NACK_THROTTLE_INTERVAL_NS		= 2_ms;

	constexpr uint64	REASSEMBLY_TIMEOUT_NS = 10_s;

	static bool SeqGreater(uint16 a, uint16 b)
	{
		return static_cast<int16>(a - b) > 0;
	}

	static uint16 SeqDistance(uint16 newer, uint16 older)
	{
		// newer - older (mod 2^16)
		return static_cast<uint16>(newer - older);
	}

	static bool SeqInWindow(uint16 base, uint16 seq, uint16 window)
	{
		// base부터 앞으로 window 범위 안인지 (wrap 고려)
		// 예: base=100, window=32 -> [100, 131] 범위
		return SeqDistance(seq, base) < window;
	}

	// ============================================================
	//	SessionInfo
	// ============================================================

	struct SessionInfo
	{
		Session*					session				= nullptr;
									
		uint64						connectedTime_ns	= 0_ns;
		uint64						lastRecvTime_ns		= 0_ns;
		uint64						lastSendTime_ns		= 0_ns;

		enum State : uint8
		{
			CONNECTING		= 0,
			CONNECTED		= 1,
			DISCONNECTING	= 2,
			DISCONNECTED	= 3
		};
		State						state				= DISCONNECTED;

		static constexpr uint64		kTimeout_ns			= 30_s;

		static SessionInfo			FromSession(Session* session, uint64 now_ns);
	};


	// ============================================================
	//  Session Auth (Principal Claim)
	// ============================================================

	struct SessionAuth
	{
		uint64	principalId		= 0;
		bool	authenticated	= false;
	};

	// ============================================================
	// Channel State Components
	// ============================================================

	/// 시퀀스 상태 - 4개 채널 모두 사용
	struct SequenceState
	{
		// global packet sequence (ACK/Retransmit 대상)
		uint16 nextPacketSeq			= 0;
		uint16 latestPacketRecvSeq		= 0;

		// channel-specific
		uint16 latestSequencedRecvSeq	= 0;	// UNRELIABLE_SEQUENCED 전용
		uint16 nextOrdredSeq			= 0;    // RELIABLE_ORDERED 전용 송신
		uint16 expectedOrderedSeq		= 0;	// RELIABLE_ORDERED 전용 수신 expected packet-seq base

		uint16 AllocPacketSeq(uint16 count = 1)
		{
			JAM_ASSERT(count < 0x8000);
				
			const uint16 base = nextPacketSeq;
			nextPacketSeq = static_cast<uint16>(nextPacketSeq + count);
			return base;
		}

		uint16 AllocOrderedSeq(uint16 count = 1)
		{
			JAM_ASSERT(count < 0x8000);

			const uint16 base = nextOrdredSeq;
			nextOrdredSeq = static_cast<uint16>(nextOrdredSeq + count);
			return base;
		}

		bool IsNewerSequenced(uint16 seq) const
		{
			return SeqGreater(seq, latestSequencedRecvSeq);
		}

		void UpdateSequencedLatest(uint16 seq)
		{
			latestSequencedRecvSeq = seq;
		}
	};

	/// 순서 보장 상태 - RELIABLE_ORDERED만 사용
	struct OrderState
	{
		struct RecvPacket
		{
			uint16							orderedSeq	= 0;
			uint16							span		= 1;
			uint64							recvTime_ns = 0_ns;
			std::shared_ptr<RecvBuffer>		buf;
		};

		std::map<uint16, RecvPacket>		pendings;

		static constexpr uint32		kMaxRecvBufferSize = 256;

		bool						StoreRecvPacket(uint16 orderedSeq, uint16 span, const std::shared_ptr<RecvBuffer>& buf, uint64 now_ns);
		std::vector<RecvPacket>		PopOrderedPackets(OUT uint16& expectedSeq);
	};

	/// 신뢰성 상태 - RELIABLE_ORDERED, RELIABLE_UNORDERED 2개만 사용
	struct ReliabilityState
	{
		struct PendingPacket
		{
			uint16							seq						= 0;
			eChannelType                    channel					= eChannelType::UNRELIABLE_UNORDERED;
			uint64							sendTime_ns				= 0;
			uint64							lastRetransmitTime_ns	= 0;
			uint8							retryCount				= 0;
			bool							hasInitialSend			= false;
			bool							hasRetransmitted		= false;
			bool							countedGiveup			= false;
			std::shared_ptr<SendBuffer>		buf;
		};

		// reliable 송신 추적 (global seq 기준)
		std::map<uint16, PendingPacket>     reliablePendings;
		uint32                              inflightSize			= 0;

		// 전역 ACK 수신 상태 (peer 전체)
		uint16                              latestRecvSeq			= 0;		// 가장 최신으로 관측한 수신 seq
		uint16                              lastAckedSeq			= 0;		// 내가 상대에게 반영한 최신 ack
		std::bitset<ACK_TRACK_SIZE>         ackTrack;							// latestRecvSeq 기준 과거 수신 상태

		bool                                ackDirty				= false;
		uint16                              pendingAckSeq			= 0;
		uint32                              pendingAckBitfield		= 0;
		uint64                              firstPendingAckTime_ns	= 0;

		// ordered gap 탐지 / NACK 보조
		std::unordered_set<uint16>          sentNackSeqs;
		uint64                              lastNackTime_ns			= 0;


		bool							StoreSendPacket(eChannelType ch, const std::shared_ptr<SendBuffer>& buf, uint16 seq, uint64 now_ns);
		std::vector<uint16>				GetRetransmitNeeded(uint64 now_ns) const;
		
		PendingPacket*					TryGetPending(uint16 seq);
		const PendingPacket*			TryGetPending(uint16 seq) const;
		void							ErasePendingPacket(uint16 seq);

		void							MarkReceived(uint16 seq, uint64 now_ns);
		void							BuildPendingAck();
		void							ProcessAck(uint16 ackSeq, uint32 ackBitfield);
		bool							ShouldSendAck(uint64 now_ns) const;
		void							ClearPendingAck();

		uint32							BuildAckWindow() const;
		uint32							BuildNackWindow(uint16 expectedSeq) const;
	};

	// ============================================================
	// Fragmentation State
	// ============================================================

	struct FragmentState
	{
		struct Reassembly
		{
			uint16							fragmentId		= 0;
			uint8							totalFragments	= 0;
			uint8							receivedCount	= 0;

			std::bitset<256>				receivedMask;
			std::vector<std::vector<BYTE>>	fragments;

			uint64							startTime_ns	= 0_ns;
			uint64							lastRecvTime_ns = 0_ns;

			PacketHeader					originalHeader  = {};
			bool							headerSaved		= false;

			Reassembly(uint16 id, uint8 total, uint64 now_ns)
				: fragmentId(id), totalFragments(total), startTime_ns(now_ns), lastRecvTime_ns(now_ns)
			{
				fragments.resize(total);
			}

			bool					AddFragment(uint8 index, const BYTE* data, uint32 size, uint64 now_ns);
			std::vector<BYTE>		Assemble() const;
			bool					IsComplete() const { return receivedCount == totalFragments; }
		};

		std::unordered_map<uint16, Reassembly>	reassemblies;
		uint64									timeoutDrops = 0;

		static constexpr uint16					kMaxFragmentSize		= JAMNET_MTU;
		static constexpr uint16					kMaxFragmentPayloadSize = kMaxFragmentSize - PacketHeader::MAX_WIRE_SIZE - sizeof(ACK_DATA);
		static constexpr uint8					kMaxFragments			= UINT8_MAX;

		bool								AddFragment(uint16 fragmentId, uint8 totalFragments, uint8 index, const BYTE* data, uint32 size, uint64 now_ns);
		std::optional<std::vector<BYTE>>	PopCompleted(uint16 fragmentId);
		void								CleanupTimeouts(uint64 now_ns);
	};

	// ============================================================
	// RPC State
	// ============================================================

	struct RpcState
	{
		struct AwaitState
		{
			std::function<void(const BYTE*, size_t)>	onPayload	= nullptr;
			std::function<void(bool)>					onDone		= nullptr;
			uint64										deadline_ns = 0_ns;
			bool										hasDeadline = false;
		};

		uint32										nextRequestId = 1;
		std::unordered_map<uint32, AwaitState>		inflight;

		uint32										GenerateRequestId() { return nextRequestId++; }
		void										RegisterRequest(uint32 reqId, AwaitState&& state);
		std::optional<AwaitState>					PopRequest(uint32 reqId);
		std::vector<uint32>							GetTimedOutRequests(uint64 now_ns) const;
	};


	// ============================================================
	// Time Synchronization
	// ============================================================

	struct TimeSyncState
	{
		int64					offset_ns			= 0;
		double					drift_ppm			= 0.0;

		uint64					lastPingSend_ns		= 0;
		uint64					lastPongRecv_ns		= 0;

		uint64					lastSampleClient_ns = 0;
		uint64					lastSampleServer_ns = 0;

		uint64					lastT1ClientSend_ns = 0;
		uint64					lastT2ServerRecv_ns = 0;
		uint64					lastT3ServerSend_ns = 0;
		uint64					lastT4ClientRecv_ns = 0;

		static constexpr uint8	WIN = 32;
		std::array<uint64, WIN>	rwnd{};
		std::array<int64, WIN>	ownd{};
		uint8					winCount = 0;
		uint8					winHead = 0;
		uint64					minRtt_ns = UINT64_MAX;

		bool					bIsServerSide = false;
		bool					bIsStabilized = false;
		uint64					currentPingInterval_ns = kPingIntervalInitial_ns;


		static constexpr uint64 kPingIntervalInitial_ns = 250_ms;
		static constexpr uint64 kPingIntervalStable_ns  = 2_s;
		static constexpr uint8  kStabilizationThreshold = 20;

		void					ProcessPingPong(uint64 t1, uint64 t2, uint64 t3, uint64 t4);
		int64					GetServerTime(uint64 clientTime_ns) const;
		bool					ShouldSendPing(uint64 now_ns) const;
	};

	// ============================================================
	// Handshake State
	// ============================================================

	struct HandshakeState
	{
		enum State : uint8
		{
			DISCONNECTED				= 0,

			CONNECT_SYN_SENT			= 1,
			CONNECT_SYN_RECEIVED		= 2,
			CONNECT_SYNACK_SENT			= 3,
			CONNECT_SYNACK_RECEIVED		= 4,
			CONNECTED					= 5,

			DISCONNECT_FIN_SENT			= 6,
			DISCONNECT_FIN_RECEIVED		= 7,
			DISCONNECT_FINACK_SENT		= 8,
			DISCONNECT_FINACK_RECEIVED	= 9,
			DISCONNECT_ACK_SENT			= 10,
			DISCONNECT_ACK_RECEIVED		= 11,

			TIME_WAIT					= 12,
			CLOSING						= 13,

			TIME_OUT					= 14,
			ERROR_STATE					= 15,
		};
		State						state				 = DISCONNECTED;
		uint64						lastHandshakeTime_ns = 0;
		uint8						retryCount			 = 0;
		uint64						timeWaitStart_ns	 = 0_ns;

		static constexpr uint64		kHandshakeTimeout_ns = 2_s;
		static constexpr uint64		kHandshakeMSL_ns	 = 12_s;
		static constexpr uint8		kMaxRetry			 = 5;
	};

	// ============================================================
	// Congestion Control
	// ============================================================

	struct CongestionState
	{
		enum State : uint8
		{
			SLOW_START			 = 0,
			CONGESTION_AVOIDANCE = 1,
			FAST_RECOVERY		 = 2,
		};
		State		state		  = SLOW_START;
		uint32		cwnd		  = 4   * JAMNET_MTU;
		uint32		ssthresh	  = 32  * JAMNET_MTU;

		uint32		minCwnd		  = 1   * JAMNET_MTU;
		uint32		maxCwnd		  = 256 * JAMNET_MTU;

		uint32		bytesInFlight = 0;
		uint32		duplicateAcks = 0;

		void		OnSend(uint32 packetSize);
		void		OnAck(uint32 ackedBytes);
		void		OnLoss();
		void		OnTimeout();
		bool		CanSend(uint32 packetSize) const;
	};

	// ============================================================
	// Transmission Queue
	// ============================================================

	struct TransmissionWaitingQueue
	{
		enum Priority : uint8
		{
			CONTROL		= 0,
			RETRANSMIT	= 1,
			ACK_ONLY	= 2,
			NORMAL		= 3,
		};

		struct PendingPacket
		{
			Priority					priority = NORMAL;
			uint32						size	 = 0;
			std::shared_ptr<SendBuffer>	buf;
		};

		std::vector<PendingPacket>		queue;
		uint64							lastFlushTime_ns = 0;
		uint64							bytesQueued		 = 0;
		bool							flushRequested	 = false;


		static constexpr uint32		kMaxTransportBatch			= 32;
		static constexpr uint64		kTransportFlushInterval_ns = 1_ms;
		static constexpr uint64		kTransportImmediateCtrl_ns = 0_ns;

		void						Enqueue(const std::shared_ptr<SendBuffer>& buf, Priority prio);
		bool						ShouldFlush(uint64 now_ns) const;
		void						Clear();
	};
	using TxPriority	  = TransmissionWaitingQueue::Priority;
	using TxPendingPacket = TransmissionWaitingQueue::PendingPacket;

} // namespace jam::net
