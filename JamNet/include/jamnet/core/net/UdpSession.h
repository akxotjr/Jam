#pragma once
#include <atomic>
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/Session.h"

namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{

	void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	class UdpSession : public Session
	{
		friend class IocpCore;
		friend class Service;

		friend void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	public:
		UdpSession();
		~UdpSession() override = default;

		bool							Connect() override;
		void							Disconnect() override;
		void							Send(Packet packet) override;

		void							OnLinkEstablished() override;
		void							OnLinkTerminated() override;

	private:
		HANDLE							GetHandle() override { return HANDLE(); }
		void							Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;
		void							OnPendingDispatchDrained() override;

	public:
		void							ProcessRecv(int32 numOfBytes, Packet packet, uint64 ingressRecvTime_ns);
		void							RegisterSend(std::vector<PacketChain>&& chains);

		void							HandleError(int32 errorCode);

	private:
		bool							RegisterConnect();
		bool							RegisterDisconnect();
		void							ProcessConnect();
		void							ProcessDisconnect();

		entt::entity					EnsureSessionEntity();

	private:
		UdpConnectEvent					m_connectEvent;
		UdpDisconnectEvent				m_disconnectEvent;
		std::atomic<bool>				m_releaseQueued{ false };
	};

} // namespace jam::net
