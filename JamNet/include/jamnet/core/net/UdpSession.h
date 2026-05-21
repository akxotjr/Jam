#pragma once
#include <atomic>

#include "jamnet/core/executor/RuntimeId.h"
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
		friend struct UdpPrebindConnectRetry;

		friend void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	public:
		UdpSession();
		~UdpSession() override = default;

		bool							Connect() override;
		void							Disconnect() override;
		void							Send(Packet packet) override;

	private:
		HANDLE							GetHandle() override { return HANDLE(); }
		void							Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;
		void							OnPendingDispatchDrained() override;

	public:
		void							ProcessRecv(int32 numOfBytes, Packet packet, uint64 ingressRecvTime_ns);
		void							RegisterSend(std::vector<PacketChain>&& chains);

		void							ProcessSystemPacket(Packet packet, const PacketHeaderView& view, uint64 ingressRecvTime_ns);
		void							SendImmediatePacket(Packet packet);

		void							HandleError(int32 errorCode);

	private:
		bool							RegisterConnect();
		bool							RegisterDisconnect();
		void							ProcessConnect();
		void							ProcessDisconnect();

		void							HandlePreBindSystemPacket(const PacketHeaderView& view) override;
		void							SchedulePreBindHandshakeRetry();
		void							AbortPreBindHandshake();

		void							TrySessionBinding();
		void							StartSessionBindingRequest();
		void							ScheduleSessionBindingRetry();

	protected:
		virtual bool					ValidateServerUdpBindPrincipal(uint64 accountId, RuntimeId userId) { (void)accountId; (void)userId; return false; }
		void							OnSessionPrincipalUpdated() override { TrySessionBinding(); }

	private:
		UdpConnectEvent					m_connectEvent;
		UdpDisconnectEvent				m_disconnectEvent;

		HandshakeState					m_prebindHandshake  = {};
		uint32							m_prebindTimerToken = 0;
	};

} // namespace jam::net
