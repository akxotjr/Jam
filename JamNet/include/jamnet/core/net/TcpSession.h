#pragma once

#include "jamnet/core/executor/RuntimeId.h"
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/TcpRecvAssembler.h"

#include <span>
#include <vector>


namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{
	class AdmissionContext;

	void FlushTransportEntity(ShardLocal& L, entt::entity entity, uint64 now_ns);
	void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	class TcpSession : public Session
	{
		friend class AdmissionContext;
		friend class TcpListener;
		friend class IocpCore;
		friend class Service;
		friend void FlushTransportEntity(ShardLocal& L, entt::entity entity, uint64 now_ns);
		friend void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	public:
		TcpSession();
		virtual ~TcpSession() override;

		bool					Connect() override;
		void					Disconnect() override;
		void					Send(Packet packet) override;

		bool					SetAuthCredential(uint32 scheme, std::span<const uint8> field0, std::span<const uint8> field1);

	private:
		HANDLE					GetHandle() override;
		void					Dispatch(IocpEvent* iocpEvent, int32 bytes = 0) override;
		void					OnPendingDispatchDrained() override;

		void					SetAdmissionContext(std::shared_ptr<AdmissionContext> context, uint64 admissionId);
		void					ClearAdmissionContext();

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
		void					HandleClientBindResponse(const PacketHeaderView& view);

		void					TrySessionBinding();
		void					ScheduleSessionBindingRetry();

		void					SendImmediatePacket(Packet packet);
		void					HandleError(int32 errorCode);

	protected:
		virtual void			OnTcpBindBootstrap(eBootstrapKind kind) { (void)kind; }
		void					OnSessionPrincipalUpdated() override { TrySessionBinding(); }

	private:
		TcpConnectEvent					m_connectEvent;
		TcpDisconnectEvent				m_disconnectEvent;

		TcpRecvAssembler				m_recvAssembler;

		TCP_BIND_REQ_DATA				m_authRequest = {};
		bool							m_hasAuthRequest = false;

		std::weak_ptr<AdmissionContext>		m_admissionContext;
		uint64								m_admissionId = 0;
	};
}
