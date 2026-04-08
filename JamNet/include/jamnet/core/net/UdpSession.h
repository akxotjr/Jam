#pragma once
#include "jamnet/core/net/Session.h"

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
		void							Send(const std::shared_ptr<SendBuffer>& buf) override;

		void							OnLinkEstablished() override;
		void							OnLinkTerminated() override;

	private:
		HANDLE							GetHandle() override { return HANDLE(); }
		void							Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override {}

	public:
		void							ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer, uint64 ingressRecvTime_ns);
		void							RegisterSend(const std::vector<std::shared_ptr<SendBuffer>>& bufs);

		void							HandleError(int32 errorCode);
	};

} // namespace jam::net

