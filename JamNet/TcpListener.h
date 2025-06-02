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

	public:
		// 외부에서 사용
		bool					StartAccept(Sptr<Service> service);
		void					CloseSocket();

	public:
		// 인터페이스 구현
		virtual HANDLE			GetHandle() override;
		virtual void			Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	private:
		// 수신 관련
		void					RegisterAccept(AcceptEvent* acceptEvent);
		void					ProcessAccept(AcceptEvent* acceptEvent);

	protected:
		SOCKET					m_socket = INVALID_SOCKET;
		xvector<AcceptEvent*>	m_acceptEvents;
		Wptr<Service>			m_service;
	};
}

