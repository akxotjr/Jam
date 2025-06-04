#pragma once
#include "Session.h"
#include <bitset>

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
		double				timestamp;
		uint32				retryCount = 0;
	};




	enum class EUdpSessionState : uint8
	{
		Connected,
		Disconnected,

		// client
		SynSent,			// 1
		SynAckReceived,		// 3
		AckSent,			// 5

		// server
		SynReceived,		// 2
		SynAckSent,			// 4
		AckReceived,		// 6


		Timeout,
	};


	enum class HandshakePacketId : uint16
	{
		C_HANDSHAKE_SYN = 1,
		S_HANDSHAKE_SYN,
		C_HANDSHAKE_SYNACK,
		S_HANDSHAKE_SYNACK,
		C_HANDSHAKE_ACK,
		S_HANDSHAKE_ACK
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
		virtual void							Send(Sptr<SendBuffer> sendBuffer) override;
		virtual void							SendReliable(Sptr<SendBuffer> sendBuffer, double timestamp);

		virtual bool							IsTcp() const override { return false; }
		virtual bool							IsUdp() const override { return true; }

		void									HandleAck(uint16 latestSeq, uint32 bitfield);
		bool									CheckAndRecordReceiveHistory(uint16 seq);
		uint32									GenerateAckBitfield(uint16 latestSeq);

	private:
		/* Iocp Object impl */
		virtual HANDLE							GetHandle() override;
		virtual void							Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	public:
		void									RegisterSend(Sptr<SendBuffer> sendbuffer);
		void									RegisterRecv();

		void									ProcessConnect();
		void									ProcessDisconnect();
		void									ProcessSend(int32 numOfBytes);
		void									ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer);


		int32									IsParsingPacket(BYTE* buffer, const int32 len);


		void									ProcessHandshake(int32 numOfBytes, RecvBuffer& recvBuffer);


		void									Update(double serverTime);
		void									CheckRetryHandshake();


		bool									IsSeqGreater(uint16 a, uint16 b) { return static_cast<int16>(a - b) > 0; }

		void									HandleError(int32 errorCode);

		/** 3-Handshake **/
		/** Client **/


		void SendHandshakeSyn();
		void OnRecvHandshakeSynAck();
		void SendHandshakeAck();

		/** Server **/
		void OnRecvHandshakeSyn();
		void SendHandshakeSynAck();
		void OnRecvHandshakeAck();

		Sptr<SendBuffer> MakeHandshakePkt(HandshakePacketId id);

	private:
		USE_LOCK

	protected:
		unordered_map<uint16, PendingPacket>	_pendingAckMap;
		bitset<1024>							_receiveHistory;

		uint16									_latestSeq = 0;
		uint16									_sendSeq = 1;			// 다음 보낼 sequence
		float									_resendIntervalMs = 0.1f; // 재전송 대기 시간

	private:
		EUdpSessionState						m_state = EUdpSessionState::Disconnected;

		//SendEvent								m_sendEvent;

		RecvBuffer								m_recvBuffer;


		// UdpSession 멤버 변수
		int32									m_handshakeRetryCount = 0;
		double									m_lastHandshakeTime = 0.0;
	};

}

