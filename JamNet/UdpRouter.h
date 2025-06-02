#pragma once
#include "IocpCore.h"

namespace jam::net
{
	class UdpRouter : public IocpObject
	{
	public:
		UdpRouter();
		~UdpRouter();

		bool                    Start(Sptr<Service> service);


		virtual HANDLE			GetHandle() override;
		virtual void			Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;


		void					RegisterSend(Sptr<SendBuffer> sendBuffer, const NetAddress& remoteAddress);
		void					RegisterRecv();

		void					ProcessSend(int32 numOfBytes);
		void					ProcessRecv(int32 numOfBytes);


	private:
		SOCKET					m_socket = INVALID_SOCKET;

		RecvBuffer				m_recvBuffer;

		SendEvent				m_sendEvent;
		RecvEvent				m_recvEvent;
		SOCKADDR_IN				m_remoteSockAddr = {};

		Wptr<Service>			m_service;
	};
}

