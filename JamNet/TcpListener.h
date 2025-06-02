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
		// �ܺο��� ���
		bool					StartAccept(Sptr<Service> service);
		void					CloseSocket();

	public:
		// �������̽� ����
		virtual HANDLE			GetHandle() override;
		virtual void			Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	private:
		// ���� ����
		void					RegisterAccept(AcceptEvent* acceptEvent);
		void					ProcessAccept(AcceptEvent* acceptEvent);

	protected:
		SOCKET					m_socket = INVALID_SOCKET;
		xvector<AcceptEvent*>	m_acceptEvents;
		Wptr<Service>			m_service;
	};
}

