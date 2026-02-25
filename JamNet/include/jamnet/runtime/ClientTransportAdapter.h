#pragma once

#include "jamnet/sync/transport/ITransportEndpoint.h"

namespace jam::net
{
	/// @brief Client 용 Transport Adapter (ITransportSystem 구현)
	/// @details ClientNetWorld 와 Client Session 간의 송수신을 중개.
	class ClientTransportAdapter : public ITransportEndpoint
	{
	public:
		ClientTransportAdapter() = default;
		~ClientTransportAdapter() override = default;

		void SetTcpSession(weak_ptr<Session> session);
		void SetUdpSession(weak_ptr<Session> session);

		void Send(const TransportInfo& info, const shared_ptr<SendBuffer>& buf) override;

		void EnumerateGroupUsers(uint32 groupId, const std::function<void(uint64)>& fn) override {};

	protected:
		void DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(std::weak_ptr<Session>)>& fn) override;

	private:
		weak_ptr<Session> m_tcp;
		weak_ptr<Session> m_udp;
	};
}
