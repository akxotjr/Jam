#pragma once

#include "jamnet/core/executor/RuntimeId.h"
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/TcpRecvAssembler.h"


namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{

	void FlushTransportEntity(ShardLocal& L, entt::entity entity, uint64 now_ns);
	void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	class TcpSession : public Session
	{
		friend class TcpListener;
		friend class IocpCore;
		friend class Service;
		friend struct TcpBindingStartRetry;

		friend void FlushTransportEntity(ShardLocal& L, entt::entity entity, uint64 now_ns);
		friend void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	public:
		TcpSession();
		virtual ~TcpSession() override;

		bool					Connect() override;
		void					Disconnect() override;
		void					Send(Packet packet) override;

		void					HandlePreBindSystemPacket(const PacketHeaderView& view) override;

	private:
		HANDLE					GetHandle() override;
		void					Dispatch(IocpEvent* iocpEvent, int32 bytes = 0) override;
		void					OnPendingDispatchDrained() override;

		bool					RegisterConnect();
		bool					RegisterDisconnect();
		void					RegisterSend(std::vector<PacketChain>&& chains);
		void					RegisterRecv();

		void					ProcessInboundConnect();
		void					ProcessOutboundConnect();
		void					ProcessDisconnect();
		void					ProcessSend(TcpSendEvent* ev, int32 bytes);
		void					ProcessRecv(TcpRecvEvent* ev, int32 bytes);

		void					ProcessSystemPacket(Packet packet, const PacketHeaderView& view, uint64 ingressRecvTime_ns = 0_ns);

		void					TrySessionBinding();
		void					ScheduleSessionBindingRetry();

		void					SendImmediatePacket(Packet packet);

		void					HandleError(int32 errorCode);

	protected:
		virtual RuntimeId		ResolveServerTcpBindUserId(uint64 accountId) { (void)accountId; return kInvalidRuntimeId; }
		void					OnSessionPrincipalUpdated() override { TrySessionBinding(); }

	private:
		TcpConnectEvent					m_connectEvent;
		TcpDisconnectEvent				m_disconnectEvent;

		TcpRecvAssembler				m_recvAssembler;
	};
}
