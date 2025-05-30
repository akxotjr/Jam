#pragma once
#include "IocpCore.h"

namespace jam::net
{
	class AcceptEvent;
	class ServerService;

	/*------------------
		  Listener
	-------------------*/

	class Listener : public IocpObject
	{
	public:
		Listener() = default;
		~Listener();

	public:
		// 외부에서 사용
		bool					StartAccept(ServiceRef service);
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
		SOCKET					_socket = INVALID_SOCKET;
		Vector<AcceptEvent*>	_acceptEvents;
		ServiceRef				_service;	// todo circular
	};
}

