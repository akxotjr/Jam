#pragma once
#include "IocpCore.h"

namespace jam::net
{
	class AcceptEvent;
	class Service;

	/*------------------
		  TcpListener
	-------------------*/

	class TcpListener : public IocpObject
	{
	public:
		TcpListener() = default;
		~TcpListener();


		bool					StartAccept(Service* service);
		void					CloseSocket();

		// IocpObject impl

		virtual HANDLE			GetHandle() override;
		virtual void			Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	private:
		// 수신 관련
		void					RegisterAccept(AcceptEvent* acceptEvent);
		void					ProcessAccept(AcceptEvent* acceptEvent);

	private:
		Service*				m_service = nullptr;

		SOCKET					m_socket = INVALID_SOCKET;
		vector<AcceptEvent*>	m_acceptEvents;
	};
}

