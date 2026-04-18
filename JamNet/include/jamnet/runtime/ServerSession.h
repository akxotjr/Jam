#pragma once
#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

#include "jamnet/runtime/world/WorldAssignmentTypes.h"
#include "jamnet/runtime/schema/RPCSchemaIds.h"

//#include "jamnet/sync/schema/gen/actor_control_generated.h"
//#include "jamnet/sync/schema/gen/actor_spawn_generated.h"
//#include "jamnet/runtime/schema/gen/binding_handshake_generated.h"
//#include "jamnet/runtime/schema/gen/world_assignment_generated.h"

namespace jam::net
{
	class ServerNetworkManager;

	class ServerTcpSession : public TcpSession
	{
	protected:
		void					OnConnected() override;
		void					OnDisconnected() override;
		void					OnSend(int32 len) override {}
		void					OnRecv(BYTE* buffer, int32 len) override {}
		void					HandleCustomPacket(const PacketHeaderView& view) override;

	public:
		void					SetNetworkManager(ServerNetworkManager* manager) { m_manager = manager; }
		void					SetUserId(uint64 userId) { m_userId = userId; }
		uint64					GetUserId() const { return m_userId; }
		void					OnTcpBindRequest(entt::entity e, const fb::fbTcpBindReq& req, uint32 requestId);

	private:
		ServerNetworkManager*	m_manager = nullptr;
		uint64					m_userId  = 0;
		WorldId					m_worldId = INVALID_WORLD_ID;
	};

	class ServerUdpSession : public UdpSession
	{
	protected:
		void					OnConnected() override;
		void					OnDisconnected() override;
		void					OnSend(int32 len) override {}
		void					OnRecv(BYTE* buffer, int32 len) override {}
		void					HandleCustomPacket(const PacketHeaderView& view) override;

	public:
		void					SetNetworkManager(ServerNetworkManager* manager) { m_manager = manager; }
		void					SetUserId(uint64 userId) { m_userId = userId; }
		uint64					GetUserId() const { return m_userId; }

		void					OnUdpBindRequest(entt::entity e, const fb::fbUdpBindReq& req, uint32 requestId);

		void					OnRequestWorldAssignmentReq(entt::entity e, const fb::fbRequestWorldAssignmentReq& req, uint32 requestId);

		void					OnSpawnActorRequest(entt::entity e, const fb::fbSpawnActorReq& req, uint32 requestId);
		void					OnDespawnActorRequest(entt::entity e, const fb::fbDespawnActorReq& req, uint32 requestId);
		void					OnPossessActorRequest(entt::entity e, const fb::fbPossessActorReq& req, uint32 requestId);
		void					OnUnpossessActorRequest(entt::entity e, const fb::fbUnpossessActorReq& req, uint32 requestId);

	private:
		ServerNetworkManager*	m_manager = nullptr;
		uint64					m_userId  = 0;
		WorldId					m_worldId = INVALID_WORLD_ID;
	};
}
