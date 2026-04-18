#pragma once
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/IocpCore.h"
#include "jamnet/core/net/NetAddress.h"

namespace jam::net
{

	class TcpSession;

	enum class eEventType : uint8
	{
		TcpConnect,
		TcpDisconnect,
		TcpAccept,
		TcpSend,
		TcpRecv,


		UdpConnect,
		UdpDisconnect,
		UdpSend,
		UdpRecv,
	};


	class IocpEvent : public OVERLAPPED
	{
	public:
		IocpEvent(eEventType type);

		void							Init();

	public:
		eEventType						m_eventType;
		std::shared_ptr<IocpObject>		m_owner;
	};


	class TcpConnectEvent : public IocpEvent
	{
	public:
		TcpConnectEvent() : IocpEvent(eEventType::TcpConnect) {}
	};

	class TcpDisconnectEvent : public IocpEvent
	{
	public:
		TcpDisconnectEvent() : IocpEvent(eEventType::TcpDisconnect) {}
	};

	class TcpAcceptEvent : public IocpEvent
	{
	public:
		TcpAcceptEvent() : IocpEvent(eEventType::TcpAccept) {}

	public:
		std::shared_ptr<TcpSession>	session = nullptr;

		// AcceptEx output buffer:
		// [optional initial data][local addr][remote addr]
		static constexpr DWORD kAddrLen = sizeof(SOCKADDR_IN) + 16;
		std::array<BYTE, kAddrLen * 2> acceptBuf = {}; // initialLen=0이면 주소용 2개만 필요
	};

	class TcpSendEvent : public IocpEvent
	{
	public:
		TcpSendEvent() : IocpEvent(eEventType::TcpSend) {}

		std::vector<WSABUF>			wsaBufs;				// TCP gather-send
		std::vector<PacketChain>	chains;

		size_t						curIndex	= 0;	// current WSABUF index
		ULONG						curOffset	= 0;	// current offset within WSABUF
		uint32						totalBytes	= 0;
	};

	class TcpRecvEvent : public IocpEvent
	{
	public:
		static constexpr uint32 k_chunkSize = 4096;

	public:
		TcpRecvEvent() : IocpEvent(eEventType::TcpRecv) {}

		WSABUF							wsaBuf = {};
		std::array<BYTE, k_chunkSize>	buffer = {};
	};



	class UdpConnectEvent : public IocpEvent
	{
	public:
		UdpConnectEvent() : IocpEvent(eEventType::UdpConnect) {}
	};


	class UdpDisconnectEvent : public IocpEvent
	{
	public:
		UdpDisconnectEvent() : IocpEvent(eEventType::UdpDisconnect) {}

	};

	class UdpSendEvent : public IocpEvent
	{
	public:
		UdpSendEvent() : IocpEvent(eEventType::UdpSend) {}

		std::vector<WSABUF>			wsaBufs;
		PacketChain					chain;
		NetAddress					remoteAddr	= {};
	};

	class UdpRecvEvent : public IocpEvent
	{
	public:
		UdpRecvEvent() : IocpEvent(eEventType::UdpRecv) {}

		WSABUF						wsaBuf			= {};
		Packet						packet			= {};
		NetAddress					remoteAddr		= {};
		int32						remoteAddrLen	= sizeof(SOCKADDR_IN);
		DWORD						flags			= 0;
	};
}
