#pragma once
#include "IocpCore.h"
#include "NetAddress.h"

namespace jam::net
{
	class Session;
	class TcpSession;
	class SendBuffer;

	enum class eEventType : uint8
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
		IocpEvent(eEventType type);

		void				Init();

	public:
		eEventType					m_eventType;
		Sptr<IocpObject>			m_owner;
	};

	/*----------------
		ConnectEvent
	-----------------*/

	class ConnectEvent : public IocpEvent
	{
	public:
		ConnectEvent() : IocpEvent(eEventType::Connect) {}
	};

	/*----------------
	  DisconnectEvent
	-----------------*/

	class DisconnectEvent : public IocpEvent
	{
	public:
		DisconnectEvent() : IocpEvent(eEventType::Disconnect) {}
	};

	/*----------------
		AcceptEvent
	-----------------*/

	class AcceptEvent : public IocpEvent
	{
	public:
		AcceptEvent() : IocpEvent(eEventType::Accept) {}

	public:
		Sptr<TcpSession>			session = nullptr;
	};

	/*----------------
		RecvEvent
	-----------------*/

	class RecvEvent : public IocpEvent
	{
	public:
		RecvEvent() : IocpEvent(eEventType::Recv) {}

	public:
		NetAddress					remoteAddress;
	};

	/*----------------
		SendEvent
	-----------------*/

	class SendEvent : public IocpEvent
	{
	public:
		SendEvent() : IocpEvent(eEventType::Send) {}

	public:
		// 단일 / 다중 중 하나만 사용
		bool                         use_gather = false;
		WSABUF                       single{};          // 단일 경로
		xvector<WSABUF>              gather;            // S/G 경로

		xvector<Sptr<SendBuffer>>    sendBuffers;       // 데이터 생존 보장
		NetAddress                   remoteAddress;
	};
}