#pragma once
#include "jamnet/core/net/IocpCore.h"

namespace jam::net
{
	struct TcpAcceptEvent;
	class Service;

	class TcpListener : public IocpObject
	{
	public:
		static constexpr int32 NumOutstanding = 4;

	public:
		TcpListener() = default;
		~TcpListener() override;


		bool						StartAccept(Service* service);
		void						CloseSocket();

		virtual HANDLE				GetHandle() override;
		virtual void				Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	private:
		void						RegisterAccept();
		void						ProcessAccept(TcpAcceptEvent* event);

	private:
		Service*					m_service	= nullptr;
		SOCKET						m_socket	= INVALID_SOCKET;
	};
}

