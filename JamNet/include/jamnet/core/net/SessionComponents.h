#pragma once

#include "jamnet/core/utils/TimeUnits.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/PacketBuilder.h"

#include <bitset>
#include <deque>

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
		static constexpr Duration Timeout = 30_s;

		enum State : uint8
		{
			CONNECTING		= 0,
			CONNECTED		= 1,
			DISCONNECTING	= 2,
			DISCONNECTED	= 3
		};

		Session*					session			= nullptr;
		Timestamp					connectedTime	= Timestamp::Zero();
		Timestamp					lastRecvTime	= Timestamp::Zero();
		Timestamp					lastSendTime	= Timestamp::Zero();
		State						state			= DISCONNECTED;


		static SessionInfo			FromSession(Session* session, Timestamp now);
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
			uint16		orderSeq = 0;
			uint16		span	 = 1;
			Timestamp	recvTime = Timestamp::Zero();
			Packet		packet;
		};

		std::map<uint16, OrderedPacket>	pendings;


		bool						StoreRecvPacket(uint16 orderSeq, uint16 span, Packet packet, Timestamp now);
		std::vector<OrderedPacket>	PopOrderedPackets(OUT uint16& expectedSeq);
	};

	struct RttState
	{
		Duration	latestRtt			= Duration::Zero();
		Duration	minRtt				= Duration::Max();
		Duration	smoothedRtt			= Duration::Zero();
		Duration	rttVariance			= Duration::Zero();
		bool		hasReliableSample	= false;

		void		OnSample(Duration sample, Duration peerDelay = Duration::Zero());

		// ACK samples drive RTO after the reliable path has produced its first unambiguous sample.
		void		OnReliableSample(Duration sample, Duration peerDelay);
		
		// Ping/Pong provides only the initial RTO estimate; time synchronization still keeps its own window.
		void		OnTimeSyncSample(Duration sample);
		Duration	GetRetransmitTimeout(uint8 retryCount) const;
	};


	struct AckData
	{
		AckHeader	header;
		AckRange	ranges[kMaxAckRanges];
	};


	/// 신뢰성 상태 - RELIABLE_ORDERED, RELIABLE_UNORDERED 2개만 사용
	struct ReliabilityState
	{
		static constexpr Duration ReliableDeliveryTimeout	= 30_s;
		static constexpr uint32 AckTrackSize				= 4096;
		static constexpr uint8  FastRetransmitThreshold		= 3;
		static constexpr uint32 AckElicitingPacketThreshold	= 2;
		static constexpr Duration DelayedAckTimeout		= 20_ms;

		struct PendingPacket
		{
			uint16							reliabilitySeq			= 0;
			eChannel						channel					= eChannel::UDP_DEFAULT;
			Timestamp						sendTime				= Timestamp::Zero();
			Timestamp						lastTransmitTime		= Timestamp::Zero();
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
		uint32                              inflightSize			= 0;		// Original reliable bytes submitted locally and not yet ACKed; no send limit.
		// reliable-only ACK receive state
		bool                                hasReliabilityRecv       = false;
		bool                                hasCumulativeAck         = false;
		uint16                              cumulativeRecvSeq        = 0;
		uint16                              latestReliabilityRecvSeq = 0;
		uint16                              lastAckedReliabilitySeq = 0;
		std::bitset<AckTrackSize>			ackTrack;							// latestReliabilityRecvSeq-relative receive state

		bool                                ackDirty				= false;
		AckData                             pendingAckData;
		uint32                              pendingAckPacketCount	= 0;
		Timestamp                           firstPendingAckTime		= Timestamp::Zero();

		bool							StoreSendPacket(eChannel ch, Packet packet, uint16 seq, Timestamp now);
		std::vector<uint16>				GetRetransmitNeeded(Timestamp now, const RttState& rtt) const;
		void							MarkInitialSend(uint16 seq, Timestamp now);

		PendingPacket*					TryGetPending(uint16 seq);
		const PendingPacket*			TryGetPending(uint16 seq) const;
		void							ErasePendingPacket(uint16 seq);

		void							MarkReceived(uint16 seq, Timestamp now);
		void							MarkAckPending(Timestamp now);
		void							BuildPendingAck();
		void							ProcessAck(const AckData& ack, Timestamp ackRecvTime, RttState& rtt);
		bool							ShouldSendAck(Timestamp now) const;
		void							ClearPendingAck();

	};

	// ============================================================
	// Fragmentation State
	// ============================================================

	struct FragmentState
	{
		static constexpr uint16				MaxFragmentSize			= JAMNET_MTU;
		static constexpr uint16				MaxFragmentPayloadSize	= MaxFragmentSize - PacketHeader::MAX_WIRE_SIZE;
		static constexpr uint8				MaxFragments			= UINT8_MAX;
		static constexpr Duration			Timeout					= 5_s;

		struct Reassembly
		{
			uint16							fragmentId		= 0;
			uint8							totalFragments	= 0;
			uint8							receivedCount	= 0;

			std::bitset<256>				receivedMask;
			std::vector<std::vector<BYTE>>	fragments;

			Timestamp						startTime	= Timestamp::Zero();
			Timestamp						lastRecvTime = Timestamp::Zero();

			PacketHeader					originalHeader  = {};
			bool							headerSaved		= false;

			Reassembly(uint16 id, uint8 total, Timestamp now)
				: fragmentId(id), totalFragments(total), startTime(now), lastRecvTime(now)
			{
				fragments.resize(total);
			}

			bool					AddFragment(uint8 index, const BYTE* data, uint32 size, Timestamp now);
			std::vector<BYTE>		Assemble() const;
			bool					IsComplete() const { return receivedCount == totalFragments; }
		};

		std::unordered_map<uint16, Reassembly>	reassemblies;
		uint64									timeoutDrops = 0;

		bool									AddFragment(uint16 fragmentId, uint8 totalFragments, uint8 index, const BYTE* data, uint32 size, Timestamp now);
		std::optional<std::vector<BYTE>>		PopCompleted(uint16 fragmentId);
		void									CleanupTimeouts(Timestamp now);
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
			Timestamp											deadline = Timestamp::Zero();
			bool												hasDeadline = false;
		};

		uint32										nextRequestId = 1;
		std::unordered_map<uint32, AwaitState>		inflight;

		uint32										GenerateRequestId();
		bool										RegisterRequest(uint32 reqId, AwaitState&& state);
		std::optional<AwaitState>					PopRequest(uint32 reqId);
		std::vector<uint32>							GetTimedOutRequests(Timestamp now) const;
	};


	// ============================================================
	// Time Synchronization
	// ============================================================

	struct TimeSyncState
	{
		static constexpr Duration PingIntervalInitial	 = 250_ms;
		static constexpr Duration PingIntervalStable	 = 2_s;
		static constexpr uint8	  StabilizationThreshold = 20;
		static constexpr uint8	  Window				 = 32;


		int64						offset_ns				= 0;
		double						drift_ppm				= 0.0;

		Timestamp					lastPingSend			= Timestamp::Zero();
		Timestamp					lastPongRecv			= Timestamp::Zero();

		uint64						lastSampleClient_ns		= 0;
		uint64						lastSampleServer_ns		= 0;

		uint64						lastT1ClientSend_ns		= 0;
		uint64						lastT2ServerRecv_ns		= 0;
		uint64						lastT3ServerSend_ns		= 0;
		uint64						lastT4ClientRecv_ns		= 0;

		std::array<Duration, Window> rwnd					= {};
		std::array<int64, Window>	ownd					= {};
		uint8						winCount				= 0;
		uint8						winHead					= 0;
		bool						bIsServerSide			= false;
		bool						bIsStabilized			= false;
		Duration					currentPingInterval	= PingIntervalInitial;


		void						ProcessPingPong(uint64 t1, uint64 t2, uint64 t3, uint64 t4, RttState& rtt);
		int64						GetServerTime(uint64 clientTime_ns) const;
		bool						ShouldSendPing(Timestamp now) const;
	};


	// ============================================================
	// Transmission Queue
	// ============================================================

	struct TransmissionWaitingQueue
	{
		static constexpr uint32		MaxTransportBatch = 32;
		static constexpr Duration	FlushInterval  = 1_ms;

		struct PendingPacket
		{
			uint32						size	 = 0;
			Packet						packet;
		};

		std::deque<PendingPacket>		queue;
		Timestamp						lastFlushTime = Timestamp::Zero();
		uint64							bytesQueued		 = 0;
		bool							flushRequested	 = false;

		void						Enqueue(Packet packet);
		bool						ShouldFlush(Timestamp now) const;
		void						Clear();
	};
	using TxPendingPacket = TransmissionWaitingQueue::PendingPacket;

} // namespace jam::net
