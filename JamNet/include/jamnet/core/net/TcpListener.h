#pragma once
#include "jamnet/core/net/IocpCore.h"

namespace jam::net
{
	class TcpAcceptEvent;
	class Service;


	class TcpListener : public IocpObject
	{
	public:
		TcpListener() = default;
		~TcpListener() override;


		bool					StartAccept(Service* service);
		void					CloseSocket();


		virtual HANDLE			GetHandle() override;
		virtual void			Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	private:
		void					RegisterAccept(TcpAcceptEvent* acceptEvent);
		void					ProcessAccept(TcpAcceptEvent* acceptEvent);

	private:
		Service*										m_service	= nullptr;
		SOCKET											m_socket	= INVALID_SOCKET;
		std::vector<std::unique_ptr<TcpAcceptEvent>>	m_events;
	};
}

