#pragma once
#include "IocpCore.h"
#include "RecvBuffer.h"

namespace jam::net
{

	class UdpRouter : public IocpObject
	{
		enum { BUFFER_SIZE = 0x10000 }; // 64KB

	public:
		UdpRouter();
		virtual ~UdpRouter();

		bool                    Start(Sptr<Service> service);

		// iocp object impl
		virtual HANDLE			GetHandle() override;
		virtual void			Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;


		void					RegisterSend(Sptr<SendBuffer> buf, const NetAddress& to);
		void					RegisterSendMany(const xvector<Sptr<SendBuffer>>& bufs, const NetAddress& to);
		void					RegisterRecv();

		void					ProcessSend(int32 numOfBytes, const NetAddress& remoteAddress);
		void					ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddress);

		void					HandleError(int32 errorCode);

	private:
		SOCKET					m_socket = INVALID_SOCKET;

		RecvBuffer				m_recvBuffer;

		SendEvent				m_sendEvent;
		RecvEvent				m_recvEvent;
		SOCKADDR_IN				m_remoteSockAddr = {};

		Wptr<Service>			m_service;
	};
}

