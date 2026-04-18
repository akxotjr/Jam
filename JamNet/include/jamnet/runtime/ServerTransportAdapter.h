#pragma once

#include "jamnet/core/executor/Lock.h"
#include "jamnet/sync/transport/ITransportEndpoint.h"


namespace jam::net
{
	enum class eProtocolType : uint8;
	class ServerNetworkManager;

	class ServerTransportAdapter : public ITransportEndpoint
	{
	public:
		ServerTransportAdapter() = default;
		~ServerTransportAdapter() override = default;

		void					SetNetworkManager(ServerNetworkManager* networkManager);
		void					Send(const TransportInfo& info, Packet packet) override;

		void					EnumerateWorldUsers(uint32 worldId, const std::function<void(uint64)>& fn) override;

	protected:
		void					DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(std::weak_ptr<Session>)>& fn) override;

	private:
		Packet					ClonePacket(const Packet& packet) const;

	private:
		USE_LOCK
		ServerNetworkManager*	m_networkManager = nullptr;
	};

}
