#pragma once

#include "jamnet/core/utils/TimeUnits.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/PacketBuilder.h"

#include <bitset>

namespace jam::net
{
	class Session;


	inline bool SeqGreater(uint16 a, uint16 b)
	{
		return static_cast<int16>(a - b) > 0;
	}

	inline uint16 SeqDistance(uint16 newer, uint16 older)
	{
		// newer - older (mod 2^16)
		return static_cast<uint16>(newer - older);
	}

	inline bool SeqInWindow(uint16 base, uint16 seq, uint16 window)
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
		static constexpr uint64		Timeout_ns = 30_s;

		enum State : uint8
		{
			CONNECTING		= 0,
			CONNECTED		= 1,
			DISCONNECTING	= 2,
			DISCONNECTED	= 3
		};

		Session*					session				= nullptr;
		uint64						connectedTime_ns	= 0_ns;
		uint64						lastRecvTime_ns		= 0_ns;
		uint64						lastSendTime_ns		= 0_ns;
		State						state				= DISCONNECTED;


		static SessionInfo			FromSession(Session* session, uint64 now_ns);
	};


	// ============================================================
	// Channel State Components
	// ============================================================

	struct SequenceState
	{
		// UNRELIABLE_SEQUENCED 전용 최신성 sequence
		uint16 nextRecencySeq			= 0;
		uint16 latestRecvRecencySeq		= 0;
		bool   hasRecvRecencySeq		= false;

		// RELIABLE_* 전용 ACK/RTX sequence
		uint16 nextReliabilitySeq		= 0;

		// RELIABLE_ORDERED 전용 전달 순서 sequence
		uint16 nextOrderSeq				= 0;
		uint16 expectedOrderSeq			= 0;

		uint16 AllocRecencySeq(uint16 count = 1)
		{
			JAM_ASSERT(count < 0x8000);
				
			const uint16 base = nextRecencySeq;
			nextRecencySeq = static_cast<uint16>(nextRecencySeq + count);
			return base;
		}

		uint16 AllocOrderSeq(uint16 count = 1)
		{
			JAM_ASSERT(count < 0x8000);

			const uint16 base = nextOrderSeq;
			nextOrderSeq = static_cast<uint16>(nextOrderSeq + count);
			return base;
		}

		uint16 AllocReliabilitySeq(uint16 count = 1)
		{
			JAM_ASSERT(count < 0x8000);

			const uint16 base = nextReliabilitySeq;
			nextReliabilitySeq = static_cast<uint16>(nextReliabilitySeq + count);
			return base;
		}

		bool IsNewerRecency(uint16 seq) const
		{
			return !hasRecvRecencySeq || SeqGreater(seq, latestRecvRecencySeq);
		}

		void UpdateLatestRecency(uint16 seq)
		{
			latestRecvRecencySeq = seq;
			hasRecvRecencySeq = true;
		}
	};

	/// 순서 보장 상태 - RELIABLE_ORDERED만 사용
	struct OrderState
	{
		static constexpr uint32	MaxRecvBufferSize = 256;

		struct OrderedPacket
		{
			uint16							orderSeq	= 0;
			uint16							span		= 1;
			uint64							recvTime_ns = 0_ns;
			Packet							packet;
		};

		std::map<uint16, OrderedPacket>		pendings;


		bool						StoreRecvPacket(uint16 orderSeq, uint16 span, Packet packet, uint64 now_ns);
		std::vector<OrderedPacket>	PopOrderedPackets(OUT uint16& expectedSeq);
	};

	/// 신뢰성 상태 - RELIABLE_ORDERED, RELIABLE_UNORDERED 2개만 사용
	struct ReliabilityState
	{
		static constexpr uint64 InitialRetransmitTimeout_ns	= 250_ms;
		static constexpr uint64 MinRetransmitTimeout_ns		= 50_ms;
		static constexpr uint64 MaxRetransmitTimeout_ns		= 1_s;
		static constexpr uint64 MaxBackoffTimeout_ns		= 2_s;
		static constexpr uint64 ReliableDeliveryTimeout_ns	= 30_s;
		static constexpr uint8  MaxBackoffShift				= 4;
		static constexpr uint32 AckTrackSize				= 2048;
		static constexpr uint32 AckWindowSize				= 64;
		static constexpr uint8  FastRetransmitThreshold		= 3;
		static constexpr uint32 AckElicitingPacketThreshold	= 2;
		static constexpr uint64 DelayPiggybackAckTimeout_ns = 20_ms;

		struct PendingPacket
		{
			uint16							reliabilitySeq			= 0;
			eChannel						channel					= eChannel::UDP_DEFAULT;
			uint64							sendTime_ns				= 0;
			uint64							lastTransmitTime_ns		= 0;
			uint8							retryCount				= 0;
			bool							hasInitialSend			= false;
			bool							retransmitQueued		= false;
			bool							hasRetransmitted		= false;
			bool							fastRetransmitRequested = false;
			bool							fastRetransmitUsed		= false;
			bool							countedGiveup			= false;
			Packet							packet;
		};

		// reliable 송신 추적 (reliable sequence 기준)
		std::map<uint16, PendingPacket>     reliablePendings;
		uint32                              inflightSize			= 0;
		uint64								smoothedRtt_ns			= 0;
		uint64								rttVariance_ns			= 0;

		// reliable-only ACK receive state
		uint16                              latestReliabilityRecvSeq = 0;
		uint16                              lastAckedReliabilitySeq = 0;
		std::bitset<AckTrackSize>			ackTrack;							// latestReliabilityRecvSeq-relative receive state

		bool                                ackDirty				= false;
		uint16                              pendingAckSeq			= 0;
		uint64                              pendingAckWindow		= 0;
		uint32                              pendingAckPacketCount	= 0;
		uint64                              firstPendingAckTime_ns	= 0;

		bool							StoreSendPacket(eChannel ch, Packet packet, uint16 seq, uint64 now_ns);
		std::vector<uint16>				GetRetransmitNeeded(uint64 now_ns) const;
		uint64							GetRetransmitTimeout(uint8 retryCount) const;
		
		PendingPacket*					TryGetPending(uint16 seq);
		const PendingPacket*			TryGetPending(uint16 seq) const;
		void							ErasePendingPacket(uint16 seq);

		void							MarkReceived(uint16 seq, uint64 now_ns);
		void							MarkAckPending(uint64 now_ns);
		void							BuildPendingAck();
		void							ProcessAck(uint16 ackSeq, uint64 ackWindow, uint64 now_ns);
		bool							ShouldSendAck(uint64 now_ns) const;
		void							ClearPendingAck();

		uint64							BuildAckWindow() const;
	};

	// ============================================================
	// Fragmentation State
	// ============================================================

	struct FragmentState
	{
		static constexpr uint16				MaxFragmentSize			= JAMNET_MTU;
		static constexpr uint16				MaxFragmentPayloadSize	= MaxFragmentSize - PacketHeader::MAX_WIRE_SIZE - sizeof(ACK_DATA);
		static constexpr uint8				MaxFragments			= UINT8_MAX;
		static constexpr uint64				Timeout_ns				= 5_s;

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

		bool									AddFragment(uint16 fragmentId, uint8 totalFragments, uint8 index, const BYTE* data, uint32 size, uint64 now_ns);
		std::optional<std::vector<BYTE>>		PopCompleted(uint16 fragmentId);
		void									CleanupTimeouts(uint64 now_ns);
	};

	// ============================================================
	// RPC State
	// ============================================================

	struct RpcState
	{
		struct AwaitState
		{
			std::function<void(const BYTE*, size_t, Packet)>	onPayload	= nullptr;
			std::function<void(bool)>							onDone		= nullptr;
			uint64												deadline_ns = 0_ns;
			bool												hasDeadline = false;
		};

		uint32										nextRequestId = 1;
		std::unordered_map<uint32, AwaitState>		inflight;

		uint32										GenerateRequestId();
		bool										RegisterRequest(uint32 reqId, AwaitState&& state);
		std::optional<AwaitState>					PopRequest(uint32 reqId);
		std::vector<uint32>							GetTimedOutRequests(uint64 now_ns) const;
	};


	// ============================================================
	// Time Synchronization
	// ============================================================

	struct TimeSyncState
	{
		static constexpr uint64 PingIntervalInitial_ns	= 250_ms;
		static constexpr uint64 PingIntervalStable_ns	= 2_s;
		static constexpr uint8  StabilizationThreshold	= 20;
		static constexpr uint8	Window					= 32;


		int64						offset_ns				= 0;
		double						drift_ppm				= 0.0;

		uint64						lastPingSend_ns			= 0;
		uint64						lastPongRecv_ns			= 0;

		uint64						lastSampleClient_ns		= 0;
		uint64						lastSampleServer_ns		= 0;

		uint64						lastT1ClientSend_ns		= 0;
		uint64						lastT2ServerRecv_ns		= 0;
		uint64						lastT3ServerSend_ns		= 0;
		uint64						lastT4ClientRecv_ns		= 0;

		std::array<uint64, Window>	rwnd					= {};
		std::array<int64, Window>	ownd					= {};
		uint8						winCount				= 0;
		uint8						winHead					= 0;
		uint64						minRtt_ns				= UINT64_MAX;

		bool						bIsServerSide			= false;
		bool						bIsStabilized			= false;
		uint64						currentPingInterval_ns	= PingIntervalInitial_ns;


		void						ProcessPingPong(uint64 t1, uint64 t2, uint64 t3, uint64 t4);
		int64						GetServerTime(uint64 clientTime_ns) const;
		bool						ShouldSendPing(uint64 now_ns) const;
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
		static constexpr uint32		MaxTransportBatch = 32;
		static constexpr uint64		FlushInterval_ns  = 1_ms;
		static constexpr uint64		ImmediateCtrl_ns  = 0_ns;

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
			Packet						packet;
			PacketChain					wire;
		};

		std::vector<PendingPacket>		queue;
		uint64							lastFlushTime_ns = 0;
		uint64							bytesQueued		 = 0;
		bool							flushRequested	 = false;

		void						Enqueue(Packet packet, Priority priority);
		bool						ShouldFlush(uint64 now_ns) const;
		void						Clear();
	};
	using TxPriority	  = TransmissionWaitingQueue::Priority;
	using TxPendingPacket = TransmissionWaitingQueue::PendingPacket;

} // namespace jam::net
