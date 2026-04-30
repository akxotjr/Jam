#include "pch.h"
#include "jamnet/core/net/TcpListener.h"

#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/memory/ObjectPool.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/WinErrorHandling.h"

namespace jam::net
{
	namespace
	{
		void ReleaseAcceptEvent(TcpAcceptEvent* event)
		{
			if (!event)
				return;

			SocketUtils::Close(event->acceptSocket);
			ObjectPool<TcpAcceptEvent>::Push(event);
		}
	}

	TcpListener::~TcpListener()
	{
		SocketUtils::Close(m_socket);
	}

	bool TcpListener::StartAccept(Service* service)
	{
		m_service = service;
		if (!m_service) return false;

		m_socket = SocketUtils::CreateSocket(eProtocolType::TCP);
		if (m_socket == INVALID_SOCKET)
			return false;

		if (m_service->RegisterIocpObject(this) == false)
			return false;

		if (SocketUtils::SetReuseAddress(m_socket, true) == false)
			return false;

		if (SocketUtils::SetLinger(m_socket, 0, 0) == false)
			return false;

		if (SocketUtils::Bind(m_socket, m_service->GetLocalTcpNetAddress()) == false)
			return false;

		if (SocketUtils::Listen(m_socket) == false)
			return false;

		for (int32 i = 0; i < NumOutstanding; ++i)
		{
			RegisterAccept();
		}

		return true;
	}

	void TcpListener::CloseSocket()
	{
		MarkClosing();
		SocketUtils::Close(m_socket);
	}

	HANDLE TcpListener::GetHandle()
	{
		return reinterpret_cast<HANDLE>(m_socket);
	}

	void TcpListener::Dispatch(IocpEvent* iocpEvent, int32 /*numOfBytes*/)
	{
		if (!iocpEvent) return;

		JAM_ASSERT(iocpEvent->m_eventType == eEventType::TcpAccept);

		TcpAcceptEvent* acceptEvent = static_cast<TcpAcceptEvent*>(iocpEvent);
		
		const ULONG_PTR completionStatus = win_error::GetOverlappedNativeStatus(*iocpEvent);
		if (!win_error::IsNativeStatusSuccess(completionStatus))
		{
			if (!IsClosing())
				win_error::LogNativeStatus("[TcpListener] Accept completion", completionStatus);
			ReleaseAcceptEvent(acceptEvent);
			RegisterAccept();
			return;
		}
		
		ProcessAccept(acceptEvent);
	}

	void TcpListener::RegisterAccept()
	{
		if (m_socket == INVALID_SOCKET || IsClosing())
			return;

		auto* event = ObjectPool<TcpAcceptEvent>::Pop();
		event->Init();

		if (!TryAddPendingDispatch())
		{
			ReleaseAcceptEvent(event);
			return;
		}

		if (false == SocketUtils::AcceptEx(
			m_socket, 
			event->acceptSocket, 
			event->acceptBuf.data(), 
			TcpAcceptEvent::DataSize, 
			TcpAcceptEvent::AddrLen,
			TcpAcceptEvent::AddrLen,
			nullptr, 
			event))
		{
			const int32 errorCode = win_error::GetLastWsaError();
			if (!win_error::IsIoPending(errorCode))
			{
				ReleasePendingDispatch();
				ReleaseAcceptEvent(event);
				RegisterAccept();
			}
		}
	}

	void TcpListener::ProcessAccept(TcpAcceptEvent* event)
	{
		if (!SocketUtils::GetAcceptExSockaddrs)
		{
			ReleaseAcceptEvent(event);
			RegisterAccept();
			return;
		}

		SOCKADDR* localSockAddr		= nullptr;
		int32	  localSockAddrLen	= 0;
		SOCKADDR* remoteSockAddr	= nullptr;
		int32	  remoteSockAddrLen = 0;

		SocketUtils::GetAcceptExSockaddrs(
			event->acceptBuf.data(),
			TcpAcceptEvent::DataSize,
			TcpAcceptEvent::AddrLen,
			TcpAcceptEvent::AddrLen,
			&localSockAddr,
			&localSockAddrLen,
			&remoteSockAddr,
			&remoteSockAddrLen);

		if (!remoteSockAddr || remoteSockAddrLen < static_cast<int32>(sizeof(SOCKADDR_IN)))
		{
			ReleaseAcceptEvent(event);
			RegisterAccept();
			return;
		}

		auto* session = m_service->CreateTcpSession(NetAddress(remoteSockAddr));
		if (!session)
		{
			ReleaseAcceptEvent(event);
			RegisterAccept();
			return;
		}

		if (!SocketUtils::SetUpdateAcceptSocket(event->acceptSocket, m_socket))
		{
			ReleaseAcceptEvent(event);
			RegisterAccept();
			return;
		}

		SOCKET acceptedSocket = event->acceptSocket;
		event->acceptSocket = INVALID_SOCKET;

		session->SetSocket(acceptedSocket);
		session->ProcessInboundConnect();

		ReleaseAcceptEvent(event);
		RegisterAccept();
	}
}
