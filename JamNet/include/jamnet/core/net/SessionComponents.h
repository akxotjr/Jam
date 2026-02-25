#pragma once
#include <bitset>

#include "RecvBuffer.h"


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
		Session*					session = nullptr;
									
		uint64						connectedTime_ns = 0;
		uint64						lastRecvTime_ns  = 0;
		uint64						lastSendTime_ns  = 0;

		enum State : uint8
		{
			CONNECTING		= 0,
			CONNECTED		= 1,
			DISCONNECTING	= 2,
			DISCONNECTED	= 3
		};
		State						state = DISCONNECTED;

		static constexpr uint64		kTimeout_ns = 30_s;
		static constexpr uint64		kKeepAlive_ns = 5_s;

		static SessionInfo			FromSession(Session* sess, uint64 now_ns);
		bool						IsTimedOut(uint64 now_ns) const { return (now_ns - lastRecvTime_ns) > kTimeout_ns; }
		bool						NeedsKeepalive(uint64 now_ns) const { return (now_ns - lastSendTime_ns) > kKeepAlive_ns; }
	};


	// ============================================================
	//  Session Auth (Principal Claim)
	// ============================================================

	struct SessionAuth
	{
		uint64	principalId = 0;
		bool	authenticated = false;
	};

	// ============================================================
	// Channel State Components - 실제 필요한 것만
	// ============================================================

	/// 시퀀스 상태 - 4개 채널 모두 사용
	struct SequenceState
	{
		array<uint16, 4> sendSeq{};			// 다음 보낼 seq (채널별)
		array<uint16, 4> latestRecvSeq{};	// 최신 받은 seq (채널별)
		array<uint16, 4> expectedSeq{};		// 예상 수신 seq (채널별)

		JAMNET_FORCE_INLINE uint16 GetNextSendSeq(eChannelType ch)
		{
			return sendSeq[E2U(ch)]++;
		}

		JAMNET_FORCE_INLINE uint16 AllocSequence(eChannelType ch, uint16 count)
		{
			JAMNET_ASSERT(count < 0x8000);

			const uint32 idx = E2U(ch);
			const uint16 base = sendSeq[idx];
			sendSeq[idx] = static_cast<uint16>(sendSeq[idx] + count);
			return base;
		}

		JAMNET_FORCE_INLINE bool IsNewer(eChannelType ch, uint16 seq) const
		{
			return SeqGreater(seq, latestRecvSeq[E2U(ch)]);
		}

		JAMNET_FORCE_INLINE bool IsExpected(eChannelType ch, uint16 seq) const
		{
			return seq == expectedSeq[E2U(ch)];
		}

		JAMNET_FORCE_INLINE void UpdateLatest(eChannelType ch, uint16 seq)
		{
			latestRecvSeq[E2U(ch)] = seq;
		}

		JAMNET_FORCE_INLINE void UpdateExpected(eChannelType ch, uint16 seq)
		{
			expectedSeq[E2U(ch)] = seq;
		}
	};

	/// 순서 보장 상태 - RELIABLE_ORDERED만 사용
	struct OrderState
	{
		struct RecvPacket
		{
			uint16					seq = 0;
			uint64					recvTime_ns = 0;
			shared_ptr<RecvBuffer>  buf;
		};

		map<uint16, RecvPacket>		pendings;

		static constexpr uint32		kMaxRecvBufferSize = 256;

		bool						StoreRecvPacket(uint16 seq, const shared_ptr<RecvBuffer>& buf, uint64 now_ns);
		vector<RecvPacket>			PopOrderedPackets(uint16& expectedSeq);
	};

	/// 신뢰성 상태 - RELIABLE_ORDERED, RELIABLE_UNORDERED 2개만 사용
	struct ReliabilityState
	{
		struct PendingPacket
		{
			uint16						seq = 0;
			uint64						sendTime_ns = 0;
			uint64						lastRetransmitTime_ns = 0;
			uint8						retryCount = 0;
			shared_ptr<SendBuffer>		buf;
		};

		// 채널별 데이터 (RELIABLE_ORDERED=2, RELIABLE_UNORDERED=3)
		struct ChannelData
		{
			// 송신 추적
			map<uint16, PendingPacket>  pendings;
			uint32						inflightSize = 0;

			// 수신 추적 (ACK)
			uint16						latestAckSeq = 0;
			uint16						lastAckedSeq = 0;
			bitset<ACK_TRACK_SIZE>		ackTrack;

			// 지연 ACK
			bool						hasPendingAck = false;
			uint16						pendingAckSeq = 0;
			uint32						pendingAckBitfield = 0;
			uint64						firstPendingAckTime_ns = 0;

			// NACK
			unordered_set<uint16>		sentNackSeqs;
			uint64						lastNackTime_ns = 0;
		};

		// RELIABLE_ORDERED와 RELIABLE_UNORDERED만 저장
		ChannelData						reliableOrdered;	// index 0
		ChannelData						reliableUnordered;	// index 1

		JAMNET_FORCE_INLINE ChannelData& GetChannelData(eChannelType ch)
		{
			return (ch == eChannelType::RELIABLE_ORDERED) ? reliableOrdered : reliableUnordered;
		}

		JAMNET_FORCE_INLINE const ChannelData& GetChannel(eChannelType ch) const
		{
			return (ch == eChannelType::RELIABLE_ORDERED) ? reliableOrdered : reliableUnordered;
		}

		bool							StoreSendPacket(eChannelType ch, const shared_ptr<SendBuffer>& buf, uint16 seq, uint64 now_ns);
		vector<uint16>					GetRetransmitNeeded(eChannelType ch, uint64 now_ns) const;
		void							ProcessAck(eChannelType ch, uint16 ackSeq, uint32 ackBitfield);
		bool							ShouldSendAck(eChannelType ch, uint64 now_ns) const;
		uint32							BuildAckWindow(eChannelType ch) const;
		uint32							BuildNackWindow(eChannelType ch, uint16 expectedSeq) const;
	};

	// ============================================================
	// Fragmentation State
	// ============================================================

	struct FragmentState
	{
		struct Reassembly
		{
			uint16					fragmentId;
			uint8					totalFragments;
			uint8					receivedCount = 0;

			bitset<256>				receivedMask;
			vector<vector<BYTE>>	fragments;

			uint64					startTime_ns;
			uint64					lastRecvTime_ns;

			PacketHeader			originalHeader{};
			bool					headerSaved = false;

			Reassembly(uint16 id, uint8 total, uint64 now_ns)
				: fragmentId(id), totalFragments(total), startTime_ns(now_ns), lastRecvTime_ns(now_ns)
			{
				fragments.resize(total);
			}

			bool					AddFragment(uint8 index, const BYTE* data, uint32 size, uint64 now_ns);
			vector<BYTE>			Assemble() const;
			bool					IsComplete() const { return receivedCount == totalFragments; }
		};

		unordered_map<uint16, Reassembly>	reassemblies;

		static constexpr uint16				kMaxFragmentSize = JAMNET_MTU;
		static constexpr uint16				kMaxFragmentPayloadSize = kMaxFragmentSize - PacketHeader::FULL_SIZE - sizeof(ACK_DATA);
		static constexpr uint8				kMaxFragments = UINT8_MAX;

		bool								AddFragment(uint16 fragmentId, uint8 totalFragments, uint8 index, const BYTE* data, uint32 size, uint64 now_ns);
		optional<vector<BYTE>>				PopCompleted(uint16 fragmentId);
		void								CleanupTimeouts(uint64 now_ns);
	};

	// ============================================================
	// RPC State
	// ============================================================

	struct RpcState
	{
		struct AwaitState
		{
			function<void(const BYTE*, size_t)>		onPayload;
			function<void(bool)>					onDone;
			uint64									deadline_ns = 0;
			bool									hasDeadline = false;
		};

		uint32										nextRequestId = 1;
		unordered_map<uint32, AwaitState>			inflight;

		uint32										GenerateRequestId() { return nextRequestId++; }
		void										RegisterRequest(uint32 reqId, AwaitState&& state);
		optional<AwaitState>						PopRequest(uint32 reqId);
		vector<uint32>								GetTimedOutRequests(uint64 now_ns) const;
	};

	// ============================================================
	// Network Statistics
	// ============================================================

	struct NetworkCounter
	{
		uint64					totalRecvBytes	 = 0;
		uint64					totalRecvPackets = 0;
		uint64					totalSendBytes	 = 0;
		uint64					totalSendPackets = 0;

		void					OnRecv(uint32 bytes);
		void					OnSend(uint32 bytes);
		float					GetRecvThroughput(uint64 interval_ns) const;
		float					GetSendThroughput(uint64 interval_ns) const;
	};

	struct CompNetworkStats
	{
		float					rtt_ms				   = 0.0f;
		float					rttMin_ms			   = FLT_MAX;
		float					rttMax_ms			   = 0.0f;
		float					rttAvg_ms			   = 0.0f;

		array<float, 32>		rttSamples			   = {};
		uint8					rttSampleIndex		   = 0;
		uint8					rttSampleCount		   = 0;

		float					jitter_ms			   = 0.0f;
		float					packetLoss			   = 0.0f;
		uint32					lostPackets			   = 0;
		uint32					totalExpected		   = 0;
		float					estimatedBandwidth_bps = 0.0f;

		struct ChannelStats
		{
			uint64 recvBytes	= 0;
			uint64 sendBytes	= 0;
			uint32 recvPackets	= 0;
			uint32 sendPackets	= 0;
		};
		array<ChannelStats, 4>	channelStats;

		void					AddRttSample(float newRtt_ms);
		void					UpdateJitter();
		void					UpdatePacketLoss(uint32 lost, uint32 expected);
		void					UpdateBandwidth(uint64 bytes, uint64 interval_ns);
		void					OnChannelRecv(eChannelType ch, uint32 bytes);
		void					OnChannelSend(eChannelType ch, uint32 bytes);
	};

	struct NetworkStatsView
	{
		float rtt_ms				= 0.0f;
		float jitter_ms				= 0.0f;
		float packetLoss			= 0.0f;
		float recvThroughput_kbps	= 0.0f;
		float sendThroughput_kbps	= 0.0f;

		static NetworkStatsView FromEntity(entt::registry& R, entt::entity e);
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
		array<uint64, WIN>		rwnd{};
		array<int64, WIN>		ownd{};
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
		uint64						timeWaitStart_ns	 = 0;

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
			Priority				priority = NORMAL;
			uint32					size = 0;
			shared_ptr<SendBuffer>	buf;
		};
		vector<PendingPacket>		queue;
		uint64						lastFlushTime_ns = 0;
		uint64						bytesQueued		 = 0;
		bool						flushRequested	 = false;


		static constexpr uint32		kMaxTransportBatch			= 32;
		static constexpr uint64		kTransportFlushInterval_ns = 1_ms;
		static constexpr uint64		kTransportImmediateCtrl_ns = 0_ns;

		void						Enqueue(const shared_ptr<SendBuffer>& buf, Priority prio);
		bool						ShouldFlush(uint64 now_ns) const;
		void						Clear();
	};
	using TxPriority	  = TransmissionWaitingQueue::Priority;
	using TxPendingPacket = TransmissionWaitingQueue::PendingPacket;

} // namespace jam::net