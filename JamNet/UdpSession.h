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
		Handshaking,
		Timeout,

		None,
	};

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
		//virtual HANDLE							GetHandle() override;
		//virtual void							Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	public:
		void									RegisterSend(Sptr<SendBuffer> sendbuffer);
		void									RegisterRecv();

		void									ProcessConnect();
		void									ProcessDisconnect();
		void									ProcessSend(int32 numOfBytes);
		void									ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer);


		int32									IsParsingPacket(BYTE* buffer, const int32 len);


		void									ProcessHandshake();


		void									Update(double serverTime);
		bool									IsSeqGreater(uint16 a, uint16 b) { return static_cast<int16>(a - b) > 0; }

		void									HandleError(int32 errorCode);

		void									SendHandshakePacket();
		void									SendAckPacket();

		void									RetryHandshake();

		void									HandleAck();
		void									UpdateRecvWindow();


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


		int32 m_handshakeStage = 1;
	};
}

