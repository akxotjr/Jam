#pragma once
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/Session.h"




namespace jam::net
{

	struct TransportHandlers;

	void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	class TcpSession : public Session
	{
		enum { BUFFER_SIZE = 0x10000 }; // 64KB

		friend class TcpListener;
		friend class IocpCore;
		friend class Service;
		friend struct TransportHandlers;

		friend void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	public:
		TcpSession();
		virtual ~TcpSession() override;

		virtual bool					Connect() override;
		virtual void					Disconnect() override;
		virtual void					Send(const std::shared_ptr<SendBuffer>& buf) override;

		void							OnLinkEstablished() override;
		void							OnLinkTerminated() override;

	private:
		
		HANDLE							GetHandle() override;
		void							Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

		bool							RegisterConnect();
		bool							RegisterDisconnect();
		void							RegisterSend(const std::vector<std::shared_ptr<SendBuffer>>& bufs);
		void							RegisterRecv();

		void							ProcessConnect();
		void							ProcessDisconnect();
		void							ProcessSend(SendEvent* ev, int32 numOfBytes);
		void							ProcessRecv(RecvEvent* ev, int32 numOfBytes);

		void                            ProcessRecvOnShard(const std::shared_ptr<RecvBuffer>& snap);

		void							HandleError(int32 errorCode);

	private:
		ConnectEvent					m_connectEvent;
		DisconnectEvent					m_disconnectEvent;

		RecvBuffer                      m_streamBuffer;
	};
}

