#pragma once
#include "IocpCore.h"
#include "NetAddress.h"

namespace jam::net
{
	class Session;
	class TcpSession;
	class SendBuffer;
	class RecvBuffer;

	enum class eEventType : uint8
	{
		CONNECT,
		DISCONNECT,
		ACCEPT,
		RECV,
		SEND
	};

	/*--------------
		IocpEvent
	---------------*/

	class IocpEvent : public OVERLAPPED
	{
	public:
		IocpEvent(eEventType type);

		void							Init();

	public:
		eEventType						m_eventType;
		std::shared_ptr<IocpObject>		m_owner;
	};

	/*----------------
		ConnectEvent
	-----------------*/

	class ConnectEvent : public IocpEvent
	{
	public:
		ConnectEvent() : IocpEvent(eEventType::CONNECT) {}
	};

	/*----------------
	  DisconnectEvent
	-----------------*/

	class DisconnectEvent : public IocpEvent
	{
	public:
		DisconnectEvent() : IocpEvent(eEventType::DISCONNECT) {}
	};

	/*----------------
		AcceptEvent
	-----------------*/

	class AcceptEvent : public IocpEvent
	{
	public:
		AcceptEvent() : IocpEvent(eEventType::ACCEPT) {}

	public:
		std::shared_ptr<TcpSession>			session = nullptr;
	};

	/*----------------
		RecvEvent
	-----------------*/

	class RecvEvent : public IocpEvent
	{
	public:
		RecvEvent() : IocpEvent(eEventType::RECV) {}

	public:
		RecvBuffer*							recvBuffer{};
		int32								fromLen = sizeof(SOCKADDR_IN);
		NetAddress							remoteAddress;
	};

	/*----------------
		SendEvent
	-----------------*/

	class SendEvent : public IocpEvent
	{
	public:
		SendEvent() : IocpEvent(eEventType::SEND) {}

	public:
		// 단일 / 다중 중 하나만 사용
		bool										use_gather = false;
		WSABUF										single{};          // 단일 경로
		std::vector<WSABUF>							gather;            // S/G 경로

		std::vector<std::shared_ptr<SendBuffer>>    sendBuffers;       // 데이터 생존 보장
		NetAddress									remoteAddress;

		// TCP partial-send 재시도용
		size_t										curIndex   = 0;   // 현재 WSABUF 인덱스
		ULONG										curOffset  = 0;  // 현재 WSABUF 내 오프셋
		uint32										totalBytes = 0;
	};
}