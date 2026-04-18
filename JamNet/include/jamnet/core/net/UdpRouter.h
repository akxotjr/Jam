#pragma once
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/IocpCore.h"
#include "jamnet/core/net/NetAddress.h"

namespace jam::net
{
	class Service;

	class UdpRouter final : public IocpObject
	{
		static constexpr int32 OUTSTANDING_RECVS = 4;


	public:
		UdpRouter() = default;
		~UdpRouter() override = default;

		bool                    Start(Service* service);
		void					CloseSocket();

		HANDLE					GetHandle() override;
		void					Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;


		void					RegisterSend(std::vector<PacketChain>&& chains, const NetAddress& to);
		void					RegisterRecv();

		void					ProcessSend(int32 numOfBytes, const NetAddress& remoteAddress);
		void					ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddress, Packet packet, uint64 ingressRecvTime_ns);

		void					HandleError(int32 errorCode);

	private:
		Service*				m_service		= nullptr;

		SOCKET					m_socket		= INVALID_SOCKET;
		SOCKADDR_IN				m_remoteSockAddr{};
	};
}

