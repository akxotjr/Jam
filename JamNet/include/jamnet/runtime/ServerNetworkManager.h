#pragma once

#include "jamnet/runtime/world/IWorldAssignmentPolicy.h"
#include "jamnet/runtime/world/WorldAssignmentTypes.h"
#include "jamnet/runtime/world/WorldDirectory.h"

#include <jampx/IPhysicsFacade.h>

namespace jam::net
{
	class ServerTcpSession;
	class ServerUdpSession;
	class ServerTransportAdapter;
	class ServerNetWorld;
	class IWorldAssignmentService;

	struct ServerConfig
	{
		NetAddress tcpAddress{ "127.0.0.1", 7777 };
		NetAddress udpAddress{ "127.0.0.1", 8888 };
		uint32 maxConnections = 1000;

		using PhysicsFactory = std::function<std::unique_ptr<px::IPhysicsFacade>()>;
		PhysicsFactory physicsFactory = nullptr;

		std::string levelPath;
	};

	class ServerNetworkManager
	{
	public:
		explicit ServerNetworkManager(const ServerConfig& config);
		~ServerNetworkManager();

		bool								Start();
		void								Stop();
		bool								IsRunning() const { return m_running.load(std::memory_order_acquire); };

		void								SetWorldAssignmentService(std::unique_ptr<IWorldAssignmentService> service);
		IWorldAssignmentService*			GetWorldAssignmentService() const { return m_assignmentService.get(); }
		WorldAssignmentResult				RequestWorldAssignment(const WorldAssignmentRequest& req);

		void								SetWorldAssignmentPolicy(std::unique_ptr<IWorldAssignmentPolicy> policy);
		IWorldAssignmentPolicy*				GetWorldAssignmentPolicy() const;

		std::shared_ptr<ServerService>		GetService() const { return m_service; }

		WorldId								ResolveWorldId(const WorldKey& key);
		WorldId								ResolveOrAllocateWorldId(const WorldKey& key, const WorldOptions& options);
		WorldKey							GetWorldKey(WorldId worldId);

		ServerNetWorld*						GetWorld(WorldId worldId);
		ServerNetWorld*						GetWorld(const WorldKey& key);
		ServerNetWorld*						GetOrCreateWorld(WorldId worldId);
		ServerNetWorld*						GetOrCreateWorld(WorldId worldId, const WorldOptions& options);
		ServerNetWorld*						GetOrCreateWorld(const WorldKey& key, const WorldOptions& options);
		void								DestroyWorld(WorldId worldId);
		void								DestroyWorld(const WorldKey& key);

		void								RegisterTcpSession(uint64 userId, const std::shared_ptr<ServerTcpSession>& tcp);
		void								RegisterUdpSession(uint64 userId, const std::shared_ptr<ServerUdpSession>& udp);
		void								UnregisterSession(uint64 userId);

		std::shared_ptr<ServerTcpSession>	FindTcpSession(uint64 userId);
		std::shared_ptr<ServerUdpSession>	FindUdpSession(uint64 userId);

		void								BroadcastPacket(const std::shared_ptr<SendBuffer>& buf, eProtocolType protocol);
		void								SendToUser(uint64 userId, const std::shared_ptr<SendBuffer>& buf, eProtocolType protocol);

		void								JoinWorld(WorldId worldId, uint64 userId);
		void								LeaveWorld(WorldId worldId, uint64 userId);
		bool								TransferWorldAsync(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId, std::function<void(WorldTransferResult)> onDone);
		WorldTransferResult					TransferWorldAwait(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId, uint64 timeout_ns = 0);
		WorldTransferResult					TransferWorld(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId);
		bool								AttachTransferCallback(uint64 userId, std::function<void(WorldTransferResult)> onDone);

		uint32								GetWorldMemberCount(WorldId worldId);
		WorldOptions						GetWorldOptions(WorldId worldId);

		void								EnumerateConnectedUsers(const std::function<void(uint64)>& fn);
		void								EnumerateWorldUsers(WorldId worldId, const std::function<void(uint64)>& fn);
		void								TryDestroyWorldIfEmpty(WorldId worldId);
		void								TryDestroyWorldIfEmpty(const WorldKey& key);
		WorldOptions						GetWorldOptions(const WorldKey& key) { return GetWorldOptions(ResolveWorldId(key)); }

	private:
		friend struct						WorldTransferAsyncState;
		struct								WorldTransferRecord;

		bool									StartServerService();
		void									StopServerService();
		std::shared_ptr<ServerNetWorld>			FindWorldShared(WorldId worldId);
		bool									ReserveWorldTransferMembership(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId, const WorldOptions& targetOptions);
		void									RollbackWorldTransferMembership(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId);
		std::shared_ptr<WorldTransferRecord>	BeginOrJoinTransfer(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId, std::function<void(WorldTransferResult)> onDone, bool& shouldStart, WorldTransferResult& immediateResult);
		void									UpdateTransferPhase(uint64 userId, uint8 phase);
		void									CompleteTransfer(uint64 userId, const WorldTransferResult& result);

	private:
		USE_LOCK

		ServerConfig								m_config			= {};

		std::shared_ptr<ServerService>				m_service			= nullptr;
		std::shared_ptr<ServerTransportAdapter>		m_tranportAdapter	= nullptr;

		std::unique_ptr<IWorldAssignmentService>    m_assignmentService	= nullptr;
		WorldDirectory								m_worldDirectory;

		std::atomic<bool>							m_running			= false;

		std::unordered_map<WorldId, std::shared_ptr<ServerNetWorld>>		m_worlds;
		std::unordered_map<WorldId, std::vector<uint64>>					m_worldMembers;
		std::atomic<uint64>													m_nextTransferAwaitKey{ 1 };
		std::unordered_map<uint64, std::shared_ptr<WorldTransferRecord>>	m_transfers;
		std::unordered_map<uint64, WorldTransferResult>						m_recentTransferResults;

		std::unordered_map<uint64, std::shared_ptr<ServerTcpSession>>		m_tcpSessions;
		std::unordered_map<uint64, std::shared_ptr<ServerUdpSession>>		m_udpSessions;
	};
}
