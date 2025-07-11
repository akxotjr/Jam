#pragma once
#include "Session.h"
#include <bitset>

#include "CongestionController.h"
#include "NetStat.h"

namespace jam::net
{
	/*--------------------------
		 ReliableUdpSession
	---------------------------*/
	struct UdpPacketHeader
	{
		uint16 size;
		uint16 id;
		uint16 sequence = 0;
	};

	struct PendingPacket
	{
		Sptr<SendBuffer>	buffer;
		uint16				sequence;
		uint64				timestamp;
		uint32				retryCount = 0;
	};

	enum class eHandshakeState : uint8
	{
		NONE,
		SYN_SENT,
		SYN_RECV,
		SYNACK_SENT,
		SYNACK_RECV,
		ACK_SENT,
		ACK_RECV,
		COMPLETE,
		TIMEOUT
	};

	enum class eRudpPacketId : uint8
	{
		C_HANDSHAKE_SYN = 1,
		S_HANDSHAKE_SYN,
		C_HANDSHAKE_SYNACK,
		S_HANDSHAKE_SYNACK,
		C_HANDSHAKE_ACK,
		S_HANDSHAKE_ACK,

		ACK,

		C_PING,
		S_PONG,

		APP_DATA
	};

	struct AckPacket
	{
		uint16 latestSeq;
		uint32 bitfield;
	};


	constexpr int32		WINDOW_SIZE = 1024;
	constexpr int32		BITFIELD_SIZE = 32;

	constexpr int32		MAX_HANDSHAKE_RETRIES = 5;
	constexpr double	HANDSHAKE_RETRY_INTERVAL = 0.5; 

	class UdpSession : public Session
	{
		enum { BUFFER_SIZE = 0x10000 }; // 64KB

		friend class UdpReceiver;
		friend class IocpCore;
		friend class Service;

	public:
		UdpSession();
		virtual ~UdpSession() override;

	public:
		virtual bool							Connect() override;
		virtual void							Disconnect(const WCHAR* cause) override;
		virtual void							Send(const Sptr<SendBuffer>& sendBuffer) override;
		virtual void							SendReliable(const Sptr<SendBuffer>& sendBuffer);

		void									HandleAck(uint16 latestSeq, uint32 bitfield);
		bool									CheckAndRecordReceiveHistory(uint16 seq);
		uint32									GenerateAckBitfield(uint16 latestSeq);

	private:
		/* Iocp Object impl */
		virtual HANDLE							GetHandle() override;
		virtual void							Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	public:

		void									ProcessConnect();
		void									ProcessDisconnect();
		void									ProcessSend(int32 numOfBytes);
		void									ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer);


		//int32									IsParsingPacket(BYTE* buffer, const int32 len);


		int32 ParseAndDispatchPackets(BYTE* buffer, int32 len);

		void DispatchPacket(UdpPacketHeader* header, uint32 len);

		void									ProcessHandshake(UdpPacketHeader* header);


		void									UpdateRetry();
		void									CheckRetryHandshake(uint64 now);
		void									CheckRetrySend(uint64 now);


		bool									IsSeqGreater(uint16 a, uint16 b) { return static_cast<int16>(a - b) > 0; }

		void									HandleError(int32 errorCode);

		/** 3-Handshake **/
		/** Client **/


		void									SendHandshakeSyn();
		void									OnRecvHandshakeSynAck();
		void									SendHandshakeAck();

		/** Server **/
		void									OnRecvHandshakeSyn();
		void									SendHandshakeSynAck();
		void									OnRecvHandshakeAck();

		Sptr<SendBuffer>						MakeHandshakePkt(eRudpPacketId id);

		Sptr<SendBuffer> MakeAckPkt(uint16 seq);

		void SendAck(uint16 seq);

		void OnRecvAck(BYTE* data, uint32 len);

		void OnRecvAppData(BYTE* data, uint32 len);




		void SendPing();
		void SendPong(uint64 clientSendTick);
		void OnRecvPing(BYTE* data, uint32 len);
		void OnRecvPong(BYTE* data, uint32 len);

	private:
		USE_LOCK

	protected:
		unordered_map<uint16, PendingPacket>	m_pendingAckMap;
		bitset<1024>							m_receiveHistory;

		uint16									m_latestSeq = 0;
		uint16									m_sendSeq = 1;			// 다음 보낼 sequence
		uint64									m_resendIntervalMs = 1; // 재전송 대기 시간

	private:
		eHandshakeState							m_handshakeState = eHandshakeState::NONE;

		RecvBuffer								m_recvBuffer;

		int32									m_handshakeRetryCount = 0;
		uint64									m_lastHandshakeTime = 0;


		Uptr<NetStatTracker>					m_netStatTracker = nullptr;
		Uptr<CongestionController>				m_congestionController = nullptr;
	};

}

