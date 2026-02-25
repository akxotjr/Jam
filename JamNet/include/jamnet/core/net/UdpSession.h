#pragma once

#include "Session.h"

namespace jam::net
{

	/*-------------------
		 UDP Session
	--------------------*/

	void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	class UdpSession : public Session
	{
		friend class IocpCore;
		friend class Service;

		friend void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	public:
		UdpSession();
		virtual ~UdpSession() override = default;

		virtual bool							Connect() override;
		virtual void							Disconnect() override;
		virtual void							Send(const shared_ptr<SendBuffer>& buf) override;

		void									OnLinkEstablished() override;
		void									OnLinkTerminated() override;

	private:
		/* Iocp Object impl */ 
		virtual HANDLE							GetHandle() override { return HANDLE(); }
		virtual void							Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override {}

	public:
		void									ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer);
		void									RegisterSend(const vector<shared_ptr<SendBuffer>>& bufs);

		void									HandleError(int32 errorCode);
	};

}

