#pragma once
#include "jamnet/core/executor/RuntimeId.h"
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/Session.h"

namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{
	class AdmissionContext;

	void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	class UdpSession : public Session
	{
		friend class AdmissionContext;
		friend class IocpCore;
		friend class Service;
		friend void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns);

	public:
		UdpSession();
		~UdpSession() override = default;

		bool							Connect() override;
		void							Disconnect() override;
		void							Send(Packet packet) override;

	private:
		HANDLE							GetHandle() override { return HANDLE(); }
		void							Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override { (void)iocpEvent; (void)numOfBytes; }
		void							OnPendingDispatchDrained() override;

	public:
		void							ProcessRecv(int32 numOfBytes, Packet packet, uint64 ingressRecvTime_ns);

	private:
		void							RegisterSend(std::vector<PacketChain>&& chains);
		void							ProcessSystemPacket(Packet packet, const PacketHeaderView& view, uint64 ingressRecvTime_ns);
		void							SendImmediatePacket(Packet packet);
		void							ProcessConnect();
		void							ProcessDisconnect();

		void							HandleUdpControlPacket(const PacketHeaderView& view);
		void							TrySessionBinding();
		void							ScheduleSessionBindingRetry();
		void							ScheduleSessionUnbindRetry();
		void							ScheduleUnbindTombstoneExpiry();
		void							SendBindRequest();
		void							SendBindResponse();
		void							SendUnbindRequest();
		void							SendUnbindResponse();
		void							AbortTransport(const char* reason);

	protected:
		void							OnSessionPrincipalUpdated() override { TrySessionBinding(); }
	private:
		uint64							m_bindTransactionId = 0;
		uint64							m_bindDeadline_ns = 0;
		bool							m_bindAccepted = false;

		uint64							m_unbindTransactionId = 0;
		uint64							m_unbindDeadline_ns = 0;
		uint32							m_unbindTimerToken = 0;
		bool							m_unbindRequestActive = false;
		bool							m_unbindTombstone = false;
	};

} // namespace jam::net
