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

		void SetTcpSession(std::weak_ptr<Session> session);
		void SetUdpSession(std::weak_ptr<Session> session);

		void Send(const TransportInfo& info, const std::shared_ptr<SendBuffer>& buf) override;

		void EnumerateWorldUsers(uint32 worldId, const std::function<void(uint64)>& fn) override {};

	protected:
		void DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(std::weak_ptr<Session>)>& fn) override;

	private:
		std::weak_ptr<Session> m_tcp;
		std::weak_ptr<Session> m_udp;
	};
}
