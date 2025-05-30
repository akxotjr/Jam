#pragma once
#include "Session.h"

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
		SendBufferRef	buffer;
		uint16			sequence;
		double			timestamp;
		uint32			retryCount = 0;
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
		virtual void							Send(SendBufferRef sendBuffer) override;
		virtual void							SendReliable(SendBufferRef sendBuffer, double timestamp);

		virtual bool							IsTcp() const override { return false; }
		virtual bool							IsUdp() const override { return true; }

		void									HandleAck(uint16 latestSeq, uint32 bitfield);
		bool									CheckAndRecordReceiveHistory(uint16 seq);
		uint32									GenerateAckBitfield(uint16 latestSeq);

	private:
		/* Iocp Object impl */
		virtual HANDLE							GetHandle() override;
		virtual void							Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	private:
		void									RegisterSend(SendBufferRef sendbuffer);


		void									ProcessConnect();
		void									ProcessDisconnect();
		void									ProcessSend(int32 numOfBytes);

		void									Update(double serverTime);
		bool									IsSeqGreater(uint16 a, uint16 b) { return static_cast<int16>(a - b) > 0; }	// util 로 뺄지

		void									HandleError(int32 errorCode);

	private:
		USE_LOCK

	protected:
		unordered_map<uint16, PendingPacket>	_pendingAckMap;
		bitset<1024>							_receiveHistory;

		uint16									_latestSeq = 0;
		uint16									_sendSeq = 1;			// 다음 보낼 sequence
		float									_resendIntervalMs = 0.1f; // 재전송 대기 시간

	private:
		SendEvent								_sendEvent;
	};
}

