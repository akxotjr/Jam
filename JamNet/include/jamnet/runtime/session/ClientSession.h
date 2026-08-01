#pragma once

#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"
#include "jamnet/runtime/social/SocialTypes.h"


namespace jam::net
{
	class ClientNetworkManager;
	class ClientTcpSession;
	class ClientUdpSession;

	class ClientTcpSession : public TcpSession
	{
	public:
		bool					IsClientSide() const override { return true; }
		void                    SetNetworkManager(ClientNetworkManager* manager) { m_manager = manager; }

		bool                    RequestEnterWorld(const EnterWorldRequest& request);
		bool                    RequestLeaveWorld(const LeaveWorldRequest& request);
		bool					SendSocialCommand(const SocialCommand& command);

	protected:
		void                    OnLinkEstablished() override;
		void                    OnDisconnected() override;
		void                    HandleCustomPacket(Packet packet) override;
	
	private:
		ClientNetworkManager*   m_manager = nullptr;
	};

	class ClientUdpSession : public UdpSession
	{
		friend class ClientNetworkManager;

	public:
		bool					IsClientSide() const override { return true; }
		void                    SetNetworkManager(ClientNetworkManager* manager) { m_manager = manager; }

	protected:
		void                    OnLinkEstablished() override;
		void                    OnDisconnected() override;
		void                    HandleCustomPacket(Packet packet) override;

	private:
		ClientNetworkManager*   m_manager = nullptr;
	};
}
