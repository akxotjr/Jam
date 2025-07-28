#pragma once
#include "Session.h"

namespace jam::net
{
	//struct TcpPacketHeader
	//{
	//	uint16 size;
	//	uint16 id;
	//};

	class TcpSession : public Session
	{
		enum { BUFFER_SIZE = 0x10000 }; // 64KB

		friend class TcpListener;
		friend class IocpCore;
		friend class Service;

	public:
		TcpSession();
		virtual ~TcpSession() override;

	public:
		virtual bool							Connect() override;
		virtual void							Disconnect(const WCHAR* cause) override;
		virtual void							Send(const Sptr<SendBuffer>& sendBuffer) override;

	private:
		/** Iocp Object impl **/

		virtual HANDLE							GetHandle() override;
		virtual void							Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;


		/** Transport **/

		bool									RegisterConnect();
		bool									RegisterDisconnect();
		void									RegisterSend();
		void									RegisterRecv();

		void									ProcessConnect();
		void									ProcessDisconnect();
		void									ProcessSend(int32 numOfBytes);
		void									ProcessRecv(int32 numOfBytes);

		int32									IsParsingPacket(BYTE* buffer, int32 len);

		void									HandleError(int32 errorCode);

	private:
		USE_LOCK

		RecvBuffer								m_recvBuffer;
		xqueue<Sptr<SendBuffer>>				m_sendQueue;
		Atomic<bool>							m_sendRegistered = false;

	private:
		ConnectEvent							m_connectEvent;
		DisconnectEvent							m_disconnectEvent;
		SendEvent								m_sendEvent;
		RecvEvent								m_recvEvent;
	};
}

