#pragma once
#include "Session.h"

namespace jam::net
{
	class TcpSession : public Session
	{
		enum { BUFFER_SIZE = 0x10000 }; // 64KB

		friend class Listener;
		friend class IocpCore;
		friend class Service;

	public:
		TcpSession();
		virtual ~TcpSession();

	public:
		virtual bool							Connect() override;
		virtual void							Disconnect(const WCHAR* cause) override;
		virtual void							Send(SendBufferRef sendBuffer) override;
		virtual bool							IsTcp() const override { return true; }
		virtual bool							IsUdp() const override { return false; }

	private:
		/* Iocp Object impl */
		virtual HANDLE							GetHandle() override;
		virtual void							Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	private:
		// 전송 관련
		bool									RegisterConnect();
		bool									RegisterDisconnect();
		void									RegisterRecv();
		void									RegisterSend();

		void									ProcessConnect();
		void									ProcessDisconnect();
		void									ProcessRecv(int32 numOfBytes);
		void									ProcessSend(int32 numOfBytes);

		int32									IsParsingPacket(BYTE* buffer, int32 len);

		void									HandleError(int32 errorCode);

	private:
		USE_LOCK

			RecvBuffer								_recvBuffer;
		Queue<SendBufferRef>					_sendQueue;
		Atomic<bool>							_sendRegistered = false;

	private:
		ConnectEvent							_connectEvent;
		DisconnectEvent							_disconnectEvent;
		RecvEvent								_recvEvent;
		SendEvent								_sendEvent;
	};
}

