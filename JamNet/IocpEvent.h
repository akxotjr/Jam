#pragma once
#include "IocpCore.h"
#include "NetAddress.h"

namespace jam::net
{
	class Session;
	class TcpSession;
	class SendBuffer;

	enum class EventType : uint8
	{
		Connect,
		Disconnect,
		Accept,
		Recv,
		Send
	};

	/*--------------
		IocpEvent
	---------------*/

	class IocpEvent : public OVERLAPPED
	{
	public:
		IocpEvent(EventType type);

		void				Init();

	public:
		EventType					m_eventType;
		Sptr<IocpObject>			m_owner;
	};

	/*----------------
		ConnectEvent
	-----------------*/

	class ConnectEvent : public IocpEvent
	{
	public:
		ConnectEvent() : IocpEvent(EventType::Connect) {}
	};

	/*----------------
	  DisconnectEvent
	-----------------*/

	class DisconnectEvent : public IocpEvent
	{
	public:
		DisconnectEvent() : IocpEvent(EventType::Disconnect) {}
	};

	/*----------------
		AcceptEvent
	-----------------*/

	class AcceptEvent : public IocpEvent
	{
	public:
		AcceptEvent() : IocpEvent(EventType::Accept) {}

	public:
		Sptr<TcpSession>			session = nullptr;
	};

	/*----------------
		RecvEvent
	-----------------*/

	class RecvEvent : public IocpEvent
	{
	public:
		RecvEvent() : IocpEvent(EventType::Recv) {}

	public:
		NetAddress					remoteAddress;
	};

	/*----------------
		SendEvent
	-----------------*/

	class SendEvent : public IocpEvent
	{
	public:
		SendEvent() : IocpEvent(EventType::Send) {}

	public:
		xvector<Sptr<SendBuffer>>	sendBuffers;
		NetAddress					remoteAddress;
	};
}

