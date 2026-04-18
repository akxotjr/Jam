#pragma once
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

		friend void FlushTransportEntity(ShardLocal& L, entt::entity entity, uint64 now_ns);
		friend void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	public:
		TcpSession();
		virtual ~TcpSession() override;

		virtual bool					Connect() override;
		virtual void					Disconnect() override;
		virtual void					Send(Packet packet) override;

		void							OnLinkEstablished() override;
		void							OnLinkTerminated() override;

	private:
		
		HANDLE							GetHandle() override;
		void							Dispatch(IocpEvent* iocpEvent, int32 bytes = 0) override;

		bool							RegisterConnect();
		bool							RegisterDisconnect();
		void							RegisterSend(std::vector<PacketChain>&& chains);
		void							RegisterRecv();

		void							ProcessConnect();
		void							ProcessDisconnect();
		void							ProcessSend(TcpSendEvent* ev, int32 bytes);
		void							ProcessRecv(const TcpRecvEvent* ev, int32 bytes);

		void							HandleError(int32 errorCode);

	private:
		TcpConnectEvent					m_connectEvent;
		TcpDisconnectEvent				m_disconnectEvent;

		TcpRecvAssembler				m_recvAssembler;
	};
}

