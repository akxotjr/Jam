#pragma once

#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

#include "jamnet/runtime/world/WorldActionTypes.h"
#include "jamnet/runtime/schema/RPCSchemaIds.h"
#include "jamnet/runtime/schema/gen/world_assignment_generated.h"

namespace jam::net
{
	class ClientNetworkManager;
	class ClientTcpSession;
	class ClientUdpSession;

	using ClientSessionBundle = SessionRefBundle<ClientTcpSession, ClientUdpSession>;

	class ClientTcpSession : public TcpSession
	{
	public:
		bool					IsClientSide() const override { return true; }
		void                    SetNetworkManager(ClientNetworkManager* manager) { m_manager = manager; }

	protected:
		void                    OnLinkEstablished() override;
		void                    OnDisconnected() override;
		void                    HandleCustomPacket(Packet packet) override;
	
	private:
		ClientNetworkManager*   m_manager	= nullptr;
	};

	class ClientUdpSession : public UdpSession
	{
		friend class ClientNetworkManager;

	public:
		bool					IsClientSide() const override { return true; }
		void                    SetNetworkManager(ClientNetworkManager* manager) { m_manager = manager; }

		void                    RequestWorldAction(const WorldActionRequest& req);
		void                    OnWorldActionResponse(std::optional<RPCTableRef<fb::fbWorldActionRes>> res);

	protected:
		void                    OnLinkEstablished() override;
		void                    OnDisconnected() override;
		void                    HandleCustomPacket(Packet packet) override;

	private:
		ClientNetworkManager*   m_manager            = nullptr;
	};
}
