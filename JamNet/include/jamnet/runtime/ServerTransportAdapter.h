#pragma once


#include "jamnet/sync/transport/ITransportEndpoint.h"


namespace jam::net
{
	class ServerNetworkManager;

	class ServerTransportAdapter : public ITransportEndpoint
	{
	public:
		ServerTransportAdapter() = default;
		~ServerTransportAdapter() override = default;

		void					SetNetworkManager(ServerNetworkManager* networkManager);
		void					Send(const TransportInfo& info, const std::shared_ptr<SendBuffer>& buf) override;

		void					EnumerateWorldUsers(uint32 worldId, const std::function<void(uint64)>& fn) override;

	protected:
		void					DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(std::weak_ptr<Session>)>& fn) override;

	private:
		std::shared_ptr<SendBuffer>	CloneBuffer(const std::shared_ptr<SendBuffer>& buf) const;

	private:
		USE_LOCK
		ServerNetworkManager*	m_networkManager = nullptr;
	};

}
