#pragma once
#include "jamnet/runtime/schema/gen/binding_handshake_generated.h"
#include "jamnet/runtime/schema/gen/matchmaking_generated.h"
#include "jamnet/sync/schema/gen/actor_control_generated.h"
#include "jamnet/sync/schema/gen/actor_spawn_generated.h"

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
		void					HandleCustomPacket(const PacketView& view) override;

	public:
		void					SetNetworkManager(ServerNetworkManager* manager) { m_manager = manager; }
		void					SetUserId(uint64 userId) { m_userId = userId; }
		uint64					GetUserId() const { return m_userId; }
		void					OnTcpBindRequest(entt::entity e, const fb::fbTcpBindReqT& req, uint32 requestId);

	private:
		ServerNetworkManager*	m_manager = nullptr;
		uint64					m_userId = 0;
		uint32					m_groupId = 0;
	};

	class ServerUdpSession : public UdpSession
	{
	protected:
		void					OnConnected() override;
		void					OnDisconnected() override;
		void					OnSend(int32 len) override {}
		void					OnRecv(BYTE* buffer, int32 len) override {}
		void					HandleCustomPacket(const PacketView& view) override;

	public:
		void					SetNetworkManager(ServerNetworkManager* manager) { m_manager = manager; }
		void					SetUserId(uint64 userId) { m_userId = userId; }
		uint64					GetUserId() const { return m_userId; }

		void					OnUdpBindRequest(entt::entity e, const fb::fbUdpBindReqT& req, uint32 requestId);

		void					OnRequestGroupIdReq(entt::entity e, const fb::fbRequestGroupIdReqT& req, uint32 requestId);

		void					OnSpawnActorRequest(entt::entity e, const fb::fbSpawnActorReqT& req, uint32 requestId);
		void					OnDespawnActorRequest(entt::entity e, const fb::fbDespawnActorReqT& req, uint32 requestId);
		void					OnPossessActorRequest(entt::entity e, const fb::fbPossessActorReqT& req, uint32 requestId);
		void					OnUnpossessActorRequest(entt::entity e, const fb::fbUnpossessActorReqT& req, uint32 requestId);

	private:
		ServerNetworkManager*	m_manager = nullptr;
		uint64					m_userId = 0;
		uint32					m_groupId = 0;
	};
}
