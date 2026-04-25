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


	struct IocpEvent : OVERLAPPED
	{
		eEventType						m_eventType;


		IocpEvent(eEventType type);
		virtual ~IocpEvent() = default;

		virtual void Init();
	};


	struct TcpConnectEvent final : IocpEvent
	{
		TcpConnectEvent() : IocpEvent(eEventType::TcpConnect) {}
	};

	struct TcpDisconnectEvent final : IocpEvent
	{
		TcpDisconnectEvent() : IocpEvent(eEventType::TcpDisconnect) {}
	};

	struct TcpAcceptEvent final : IocpEvent
	{
		static constexpr DWORD			AddrLen			= sizeof(SOCKADDR_IN) + 16;
		static constexpr DWORD			DataSize		= 0;


		SOCKET							acceptSocket	= INVALID_SOCKET;
		std::array<BYTE, AddrLen * 2>	acceptBuf		= {};


		TcpAcceptEvent() : IocpEvent(eEventType::TcpAccept) {}

		void Init() override;
	};

	struct TcpSendEvent final : IocpEvent
	{
		std::vector<WSABUF>				wsaBufs;			// TCP gather-send
		std::vector<PacketChain>		chains;

		size_t							curIndex	= 0;	// current WSABUF index
		ULONG							curOffset	= 0;	// current offset within WSABUF
		uint32							totalBytes	= 0;


		TcpSendEvent() : IocpEvent(eEventType::TcpSend) {}

		void Init() override;
	};

	struct TcpRecvEvent final : IocpEvent
	{
		static constexpr uint32			ChunkSize = 4096;

		WSABUF							wsaBuf		= {};
		std::array<BYTE, ChunkSize>		buffer		= {};


		TcpRecvEvent() : IocpEvent(eEventType::TcpRecv) {}

		void Init() override;
	};



	struct UdpConnectEvent final : IocpEvent
	{
		UdpConnectEvent() : IocpEvent(eEventType::UdpConnect) {}
	};


	struct UdpDisconnectEvent final : IocpEvent
	{
		UdpDisconnectEvent() : IocpEvent(eEventType::UdpDisconnect) {}
	};

	struct UdpSendEvent final : IocpEvent
	{
		std::vector<WSABUF>			wsaBufs;
		PacketChain					chain;
		NetAddress					remoteAddr	= {};

		UdpSendEvent() : IocpEvent(eEventType::UdpSend) {}

		void Init() override;
	};

	struct UdpRecvEvent final : IocpEvent
	{
		WSABUF						wsaBuf			= {};
		Packet						packet			= {};
		NetAddress					remoteAddr		= {};
		int32						remoteAddrLen	= sizeof(SOCKADDR_IN);
		DWORD						flags			= 0;

		UdpRecvEvent() : IocpEvent(eEventType::UdpRecv) {}

		void Init() override;
	};
}
