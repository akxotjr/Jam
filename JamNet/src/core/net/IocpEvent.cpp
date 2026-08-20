#include "pch.h"
#include "jamnet/core/net/IocpEvent.h"

#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/SocketUtils.h"

namespace jam::net
{
	IocpEvent::IocpEvent(eEventType type) : m_eventType(type)
	{
		IocpEvent::Init();
	}

	void IocpEvent::Init()
	{
		OVERLAPPED::hEvent		 = nullptr;
		OVERLAPPED::Internal	 = 0;
		OVERLAPPED::InternalHigh = 0;
		OVERLAPPED::Offset		 = 0;
		OVERLAPPED::OffsetHigh	 = 0;
	}

	void TcpAcceptEvent::Init()
	{
		IocpEvent::Init();

		SocketUtils::Close(acceptSocket);
		acceptSocket = SocketUtils::CreateSocket(eProtocolType::TCP);
	}

	void TcpSendEvent::Init()
	{
		IocpEvent::Init();

		wsaBufs.clear();
		chains.clear();
		curIndex   = 0;
		curOffset  = 0;
		totalBytes = 0;
	}

	void TcpRecvEvent::Init()
	{
		IocpEvent::Init();

		wsaBuf = {};
	}

	void UdpSendEvent::Init()
	{
		IocpEvent::Init();
		if (wsaBufs.capacity() == 0)
			wsaBufs.reserve(8);
		wsaBufs.clear();
		chain.Clear();
		remoteAddr = {};
	}

	void UdpRecvEvent::Init()
	{
		IocpEvent::Init();

		reservation.Reset();
		wsaBuf			= {};
		remoteAddr		= {};
		remoteAddrLen	= sizeof(SOCKADDR_IN);
		flags			= 0;
	}
}
