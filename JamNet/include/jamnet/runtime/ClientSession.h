#pragma once

#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

#include "jamnet/runtime/world/WorldAssignmentTypes.h"
#include "jamnet/runtime/schema/RPCSchemaIds.h"
#include "jamnet/runtime/schema/gen/binding_handshake_generated.h"
#include "jamnet/runtime/schema/gen/world_assignment_generated.h"

namespace jam::net
{
	class ClientNetworkManager;

	class ClientTcpSession : public TcpSession
	{
	protected:
		void                    OnConnected() override;
		void                    OnDisconnected() override;
		void                    OnSend(int32 len) override {}
		void                    OnRecv(BYTE* buffer, int32 len) override {}
		void                    HandleCustomPacket(const PacketHeaderView& view) override;

	public:
		void                    SetNetworkManager(ClientNetworkManager* manager) { m_manager = manager; }
		void					SetUserId(uint64 userId) { m_userId = userId; }
		uint64					GetUserId() const { return m_userId; }

	private:
		void                    RequestTcpBind();
		void                    OnTcpBindResponse(std::optional<RPCTableRef<fb::fbTcpBindRes>> res);

	private:
		ClientNetworkManager*   m_manager = nullptr;
		uint64                  m_userId = 0;
	};

	class ClientUdpSession : public UdpSession
	{
		friend class ClientNetworkManager;

	protected:
		void                    OnConnected() override;
		void                    OnDisconnected() override;
		void                    OnSend(int32 len) override {}
		void                    OnRecv(BYTE* buffer, int32 len) override {}
		void                    HandleCustomPacket(const PacketHeaderView& view) override;

	public:
		void                    SetNetworkManager(ClientNetworkManager* manager) { m_manager = manager; }
		void					SetUserId(uint64 userId) { m_userId = userId; }
		uint64					GetUserId() const { return m_userId; }

		void                    RequestAutoAssignWorld();
		void                    RequestJoinWorld(const WorldKey& targetWorld);
		void                    RequestLeaveWorld();
		void                    RequestTransferWorld(const WorldKey& targetWorld);

	private:
		void                    RequestUdpBind();
		void                    RequestWorldAction(fb::fbWorldRequestAction action, const WorldKey& targetWorld = INVALID_WORLD_KEY);

		void                    OnUdpBindResponse(std::optional<RPCTableRef<fb::fbUdpBindRes>> res);
		void                    OnRequestWorldAssignmentRes(std::optional<RPCTableRef<fb::fbRequestWorldAssignmentRes>> res);

	private:
		ClientNetworkManager*   m_manager            = nullptr;
		uint64                  m_userId             = 0;
		WorldId                 m_worldId            = INVALID_WORLD_ID;
		uint8                   m_pendingWorldAction = static_cast<uint8>(fb::fbWorldRequestAction_AutoAssign);
	};
}
