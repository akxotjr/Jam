#pragma once
#include <bitset>

namespace jam::net
{
	constexpr uint32 WINDOW_SIZE = 1024;		// Maximum number of packets to track
	constexpr uint32 BITFIELD_SIZE = 32;		// Size of the bitfield for ACKs
	constexpr uint32 MAX_RETRY_COUNT = 5;		// Maximum number of retries for a packet
	constexpr uint64 RETRANSMIT_INTERVAL = 5;	// Resend interval in ticks (30 ticks = 1s)	// todo : make it configurable and tuning
	constexpr uint64 RETRANSMIT_TIMEOUT = 10;
	constexpr uint64 MAX_DELAY_TICK_PIGGYBACK_ACK = 5;

	struct PendingPacketInfo
	{
		Sptr<SendBuffer>	buffer;
		uint32				size;
		uint64				timestamp;
		uint32				retryCount = 0;
	};

	/**
	 *@brief
	 *	ReliableTransportManager handles reliable packet transmission, tracking pending packets,
	 *	retransmissions, and acknowledgments.
	 */

	class ReliableTransportManager
	{
	public:
		ReliableTransportManager(UdpSession* owner) : m_owner(owner) {}
		~ReliableTransportManager() = default;


		void			Update();

		// Send
		uint16			GetNextSendSeq() { return m_sendSeq.fetch_add(1, std::memory_order_relaxed); }
		uint16			AllocateSequnceRange(uint8 count) { return m_sendSeq.fetch_add(count, std::memory_order_relaxed); }

		void			AddPendingPacket(uint16 seq, const Sptr<SendBuffer>& buf, uint64 timestamp);

		// Recv
		uint32			GenerateAckBitfield(uint16 latestSeq) const;
		bool			IsSeqReceived(uint16 seq);

		// Retransmit
		uint32			GetInFlightSize() const { return m_inFlightSize; }

		// Handle ACKs
		void			SendAck();
		void			OnRecvAck(uint16 latestSeq, uint32 bitfield);

		// Piggyback ACKs
		void			SetPendingAck(uint16 seq);
		bool			HasPendingAck() const { return m_hasPendingAck; }
		void			ClearPendingAck();
		bool			ShouldSendImmediateAck(uint64 currentTick);
		bool			TryAttachPiggybackAck(const Sptr<SendBuffer>& buf);
		void			FailedAttachPiggybackAck();

		uint16			GetPendigAckSeq() const { return m_pendingAckSeq; }
		uint32			GetPendingAckBitfield() const { return m_pendingAckBitfield; }



		// Fast RTX
		void OnRecvNack(uint16 missingSeq, uint32 bitfield);
		void CheckFastRTX(uint16 ackSeq);
		void TriggerFastRTX(uint16 seq);

		uint32 GenerateNackBitfield(uint16 missingSeq);

		// NACK
		void SendNack(uint16 missingSeq, uint32 bitfield);
		bool ShouldSendNack(uint16 expectedSeq, uint16 receivedSeq);


	private:
		bool			IsSeqGreator(uint16 a, uint16 b) const { return static_cast<int16>(a - b) > 0; }

	private:
		UdpSession*							m_owner = nullptr;

		// Management of Sequence
		Atomic<uint16>						m_sendSeq{ 1 };				// Next sequence to send
		uint16								m_latestSeq = 0;			// Latest sequence received
		std::bitset<WINDOW_SIZE>			m_receiveHistory;			// History of received packets

		// Management of Reliability
		uint32								m_inFlightSize = 0;			// Total bytes in flight
		xumap<uint16, PendingPacketInfo>	m_pendingPackets;			// Map of pending packets by sequence number

		// Management of ACKs
		bool								m_hasPendingAck = false;	// Flag to indicate if there are pending ACKs
		uint16								m_pendingAckSeq = 0;		// Sequence number of the pending ACK
		uint32								m_pendingAckBitfield = 0;	// Bitfield of ACKs to send
		uint64								m_firstPendingAckTick = 0;	// Timestamp of the first pending ACK


		// Fast RTX
		constexpr static uint32 DUPLICATE_ACK_THRESHOLD = 3;
		xumap<uint16, uint32>   m_duplicateAckCount;    // 시퀀스별 중복 ACK 카운트
		uint16                  m_lastAckedSeq = 0;     // 마지막으로 ACK된 시퀀스

		// NACK
		uint16                  m_expectedNextSeq = 1;  // 다음에 받을 것으로 예상되는 시퀀스
		uint64                  m_lastNackTime = 0;     // 마지막 NACK 전송 시간
		constexpr static uint64 NACK_THROTTLE_INTERVAL = 10; // NACK 전송 간격 제한

		xuset<uint16>			m_sentNackSeqs;
	};
}

      