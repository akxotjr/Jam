#pragma once
#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

#include "jamnet/runtime/schema/RPCSchemaIds.h"


namespace jam::net
{
	class ServerNetworkManager;
	class ServerTcpSession;
	class ServerUdpSession;

	using ServerSessionBundle = SessionRefBundle<ServerTcpSession, ServerUdpSession>;

	class ServerTcpSession : public TcpSession
	{
	public:
		bool					IsServerSide() const override { return true; }
		void					SetNetworkManager(ServerNetworkManager* manager) { m_manager = manager; }

	protected:
		void					OnLinkEstablished() override;
		void					OnDisconnected() override;
		void					HandleCustomPacket(Packet packet) override;
		RuntimeId				ResolveServerTcpBindUserId(uint64 accountId) override;

	private:
		void					FinalizeEstablishedSession();
		void					BootstrapRPC();

	private:
		ServerNetworkManager*	m_manager = nullptr;
	};

	class ServerUdpSession : public UdpSession
	{
	public:
		bool					IsServerSide() const override { return true; }
		void					SetNetworkManager(ServerNetworkManager* manager) { m_manager = manager; }

		void					OnWorldActionRequest(entt::entity e, const fb::fbWorldActionReq& req, uint32 requestId);

		void					OnSpawnActorRequest(entt::entity e, const fb::fbSpawnActorReq& req, uint32 requestId);
		void					OnDespawnActorRequest(entt::entity e, const fb::fbDespawnActorReq& req, uint32 requestId);
		void					OnPossessActorRequest(entt::entity e, const fb::fbPossessActorReq& req, uint32 requestId);
		void					OnUnpossessActorRequest(entt::entity e, const fb::fbUnpossessActorReq& req, uint32 requestId);

	protected:
		void					OnLinkEstablished() override;
		void					OnDisconnected() override;
		void					HandleCustomPacket(Packet packet) override;
		bool					ValidateServerUdpBindPrincipal(uint64 accountId, RuntimeId userId) override;

	private:
		void					FinalizeEstablishedSession();
		void					BootstrapRPC();

	private:
		ServerNetworkManager*	m_manager = nullptr;
	};
}
