#pragma once
#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

#include "jamnet/runtime/protocol/schema/RPCSchemaIds.h"
#include "jamnet/runtime/protocol/schema/gen/actor_spawn_generated.h"


namespace jam::net
{
	struct UserContext;
	class ServerNetworkManager;
	class ServerTcpSession;
	class ServerUdpSession;

	using ServerSessionBundle = SessionRefBundle<ServerTcpSession, ServerUdpSession>;
	ServerSessionBundle ResolveUserSessionBundle(const UserContext& user);

	class ServerTcpSession : public TcpSession
	{
	public:
		bool					IsServerSide() const override { return true; }
		void					SetNetworkManager(ServerNetworkManager* manager) { m_manager = manager; }

		void					OnSpawnPlayerRequest(entt::entity e, const fb::fbSpawnPlayerReq& req, uint32 requestId);
		void					OnDespawnPlayerRequest(entt::entity e, const fb::fbDespawnPlayerReq& req, uint32 requestId);

	protected:
		void					OnSessionEstablished() override;
		void					OnSessionReleased() override;
		void					HandleCustomPacket(Packet packet) override;
	private:
		void					AttachToUserContext();
		void					BootstrapRPC();

	private:
		ServerNetworkManager*	m_manager = nullptr;
	};

	class ServerUdpSession : public UdpSession
	{
	public:
		bool					IsServerSide() const override { return true; }
		void					SetNetworkManager(ServerNetworkManager* manager) { m_manager = manager; }

		void					OnDespawnActorRequest(entt::entity e, const fb::fbDespawnActorReq& req, uint32 requestId);
		void					OnSpawnActorRequest(entt::entity e, const fb::fbSpawnActorReq& req, uint32 requestId);

	protected:
		void					OnSessionEstablished() override;
		void					OnSessionReleased() override;
		void					HandleCustomPacket(Packet packet) override;

	private:
		void					AttachToUserContext();
		void					BootstrapRPC();

	private:
		ServerNetworkManager*	m_manager = nullptr;
	};
}
