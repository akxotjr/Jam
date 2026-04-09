#include "pch.h"
#include "jamnet/runtime/ServerNetworkManager.h"

#include "jamnet/runtime/ServerSession.h"
#include "jamnet/runtime/ServerTransportAdapter.h"
#include "jamnet/runtime/world/DefaultWorldAssignmentService.h"

#include "jamnet/sync/networld/ServerNetWorld.h"
#include "jamnet/sync/replication/ReplicationTypes.h"

namespace jam::net
{
	ServerNetworkManager::ServerNetworkManager(const ServerConfig& config)
		: m_config(config)
	{
		m_tranportAdapter = std::make_shared<ServerTransportAdapter>();

		if (m_tranportAdapter)
			m_tranportAdapter->SetNetworkManager(this);

		SetWorldAssignmentService(std::make_unique<DefaultWorldAssignmentService>());
	}

	ServerNetworkManager::~ServerNetworkManager()
	{
		Stop();
	}

	bool ServerNetworkManager::Start()
	{
		if (m_running.load(std::memory_order_acquire))
			return true;

		if (!StartServerService())
			return false;

		m_running.store(true, std::memory_order_release);

		JAMNET_LOG_INFO("ServerNetworkManager started successfully");
		return true;
	}

	void ServerNetworkManager::Stop()
	{
		if (!m_running.exchange(false, std::memory_order_acq_rel))
			return;

		if (m_tranportAdapter)
			m_tranportAdapter.reset();

		{
			WRITE_LOCK
			m_tcpSessions.clear();
			m_udpSessions.clear();
			m_worldMembers.clear();
			m_worlds.clear();
			m_worldDirectory = {};
		}

		StopServerService();

		if (m_assignmentService)
		{
			m_assignmentService->Init(nullptr);
			m_assignmentService.reset();
		}

		JAMNET_LOG_INFO("ServerNetworkManager stopped");
	}

	void ServerNetworkManager::SetWorldAssignmentService(std::unique_ptr<IWorldAssignmentService> service)
	{
		if (m_assignmentService)
			m_assignmentService->Init(nullptr);

		m_assignmentService = std::move(service);

		if (m_assignmentService)
			m_assignmentService->Init(this);
	}

	WorldAssignmentResult ServerNetworkManager::RequestWorldAssignment(const WorldAssignmentRequest& req)
	{
		if (!m_assignmentService)
			return {};

		return m_assignmentService->AssignPrincipal(req);
	}

	void ServerNetworkManager::SetWorldAssignmentPolicy(std::unique_ptr<IWorldAssignmentPolicy> policy)
	{
		if (!m_assignmentService)
			SetWorldAssignmentService(std::make_unique<DefaultWorldAssignmentService>());

		if (m_assignmentService)
			m_assignmentService->SetWorldAssignmentPolicy(std::move(policy));
	}

	IWorldAssignmentPolicy* ServerNetworkManager::GetWorldAssignmentPolicy() const
	{
		return m_assignmentService ? m_assignmentService->GetWorldAssignmentPolicy() : nullptr;
	}

	WorldId ServerNetworkManager::ResolveWorldId(const WorldKey& key)
	{
		if (!key.IsValid())
			return INVALID_WORLD_ID;

		READ_LOCK
		return m_worldDirectory.FindWorldId(key);
	}

	WorldId ServerNetworkManager::ResolveOrAllocateWorldId(const WorldKey& key, const WorldOptions& options)
	{
		if (!key.IsValid())
			return INVALID_WORLD_ID;

		WRITE_LOCK
		return m_worldDirectory.FindOrAddWorld(key, options);
	}

	WorldKey ServerNetworkManager::GetWorldKey(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return INVALID_WORLD_KEY;

		READ_LOCK
		return m_worldDirectory.FindWorldKey(worldId);
	}

	ServerNetWorld* ServerNetworkManager::GetWorld(WorldId worldId)
	{
		READ_LOCK
		auto it = m_worlds.find(worldId);
		return (it != m_worlds.end()) ? it->second.get() : nullptr;
	}

	ServerNetWorld* ServerNetworkManager::GetWorld(const WorldKey& key)
	{
		return GetWorld(ResolveWorldId(key));
	}

	ServerNetWorld* ServerNetworkManager::GetOrCreateWorld(WorldId worldId)
	{
		return GetOrCreateWorld(worldId, GetWorldOptions(worldId));
	}

	ServerNetWorld* ServerNetworkManager::GetOrCreateWorld(WorldId worldId, const WorldOptions& options)
	{
		if (worldId == INVALID_WORLD_ID)
			return nullptr;

		WRITE_LOCK
		m_worldDirectory.SetWorldOptions(worldId, options);

		auto& slot = m_worlds[worldId];
		if (!slot)
		{
			slot = std::make_shared<ServerNetWorld>();
			slot->SetWorldId(worldId);
			slot->SetTransportAdapter(m_tranportAdapter.get());

			if (m_config.physicsFactory)
			{
				if (auto phys = m_config.physicsFactory())
					slot->SetPhysicsFacade(std::move(phys));
			}

			slot->SetLevelPath(m_config.levelPath);
			slot->Init();
			slot->Tick(SIMULATION_TICK_NS);
		}

		return slot.get();
	}

	ServerNetWorld* ServerNetworkManager::GetOrCreateWorld(const WorldKey& key, const WorldOptions& options)
	{
		return GetOrCreateWorld(ResolveOrAllocateWorldId(key, options), options);
	}

	void ServerNetworkManager::DestroyWorld(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return;

		std::shared_ptr<ServerNetWorld> victim;
		{
			WRITE_LOCK
			auto it = m_worlds.find(worldId);
			if (it == m_worlds.end())
				return;

			victim = std::move(it->second);
			m_worlds.erase(it);
			m_worldMembers.erase(worldId);
			m_worldDirectory.RemoveWorld(worldId);
		}

		if (victim)
			victim->Stop();
	}

	void ServerNetworkManager::DestroyWorld(const WorldKey& key)
	{
		DestroyWorld(ResolveWorldId(key));
	}

	void ServerNetworkManager::RegisterTcpSession(uint64 userId, const std::shared_ptr<ServerTcpSession>& tcp)
	{
		if (!userId || !tcp)
			return;

		WRITE_LOCK
		m_tcpSessions[userId] = tcp;

		JAMNET_LOG_INFO("UserId = {}] TCP Session registered", userId);
	}

	void ServerNetworkManager::RegisterUdpSession(uint64 userId, const std::shared_ptr<ServerUdpSession>& udp)
	{
		if (!userId || !udp)
			return;

		WRITE_LOCK
		m_udpSessions[userId] = udp;

		JAMNET_LOG_INFO("UserId = {}] UDP Session registered", userId);
	}

	void ServerNetworkManager::UnregisterSession(uint64 userId)
	{
		if (!userId)
			return;

		std::vector<WorldId> worldsToNotify;
		{
			WRITE_LOCK
			m_tcpSessions.erase(userId);
			m_udpSessions.erase(userId);

			for (auto& [worldId, members] : m_worldMembers)
			{
				if (std::erase(members, userId) > 0)
					worldsToNotify.push_back(worldId);
			}
		}

		for (WorldId worldId : worldsToNotify)
		{
			if (auto* world = GetWorld(worldId))
				world->Leave(userId);

			TryDestroyWorldIfEmpty(worldId);
		}

		JAMNET_LOG_INFO("UserId = {}] Session unregistered", userId);
	}

	std::shared_ptr<ServerTcpSession> ServerNetworkManager::FindTcpSession(uint64 userId)
	{
		READ_LOCK
		auto it = m_tcpSessions.find(userId);
		return (it != m_tcpSessions.end()) ? it->second : nullptr;
	}

	std::shared_ptr<ServerUdpSession> ServerNetworkManager::FindUdpSession(uint64 userId)
	{
		READ_LOCK
		auto it = m_udpSessions.find(userId);
		return (it != m_udpSessions.end()) ? it->second : nullptr;
	}

	void ServerNetworkManager::BroadcastPacket(const std::shared_ptr<SendBuffer>& buf, eProtocolType protocol)
	{
		if (!buf)
			return;

		READ_LOCK

		if (protocol == eProtocolType::TCP)
		{
			for (auto& session : m_tcpSessions | std::views::values)
			{
				if (session && session->IsConnected())
					session->Send(buf);
			}
		}

		if (protocol == eProtocolType::UDP)
		{
			for (auto& session : m_udpSessions | std::views::values)
			{
				if (session && session->IsConnected())
					session->Send(buf);
			}
		}
	}

	void ServerNetworkManager::SendToUser(uint64 userId, const std::shared_ptr<SendBuffer>& buf, eProtocolType protocol)
	{
		if (!buf)
			return;

		if (protocol == eProtocolType::TCP)
		{
			if (auto tcp = FindTcpSession(userId))
			{
				if (tcp->IsConnected())
					tcp->Send(buf);
			}
		}

		if (protocol == eProtocolType::UDP)
		{
			if (auto udp = FindUdpSession(userId))
			{
				if (udp->IsConnected())
					udp->Send(buf);
			}
		}
	}

	void ServerNetworkManager::JoinWorld(WorldId worldId, uint64 userId)
	{
		if (worldId == INVALID_WORLD_ID || userId == 0)
			return;

		WRITE_LOCK
		auto& members = m_worldMembers[worldId];
		if (std::ranges::find(members, userId) == members.end())
			members.push_back(userId);
	}

	void ServerNetworkManager::LeaveWorld(WorldId worldId, uint64 userId)
	{
		if (worldId == INVALID_WORLD_ID || userId == 0)
			return;

		{
			WRITE_LOCK
			auto it = m_worldMembers.find(worldId);
			if (it == m_worldMembers.end())
				return;

			std::erase(it->second, userId);
			if (!it->second.empty())
				return;

			m_worldMembers.erase(it);
		}

		TryDestroyWorldIfEmpty(worldId);
	}

	uint32 ServerNetworkManager::GetWorldMemberCount(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return 0;

		READ_LOCK
		auto it = m_worldMembers.find(worldId);
		return (it != m_worldMembers.end()) ? static_cast<uint32>(it->second.size()) : 0;
	}

	WorldOptions ServerNetworkManager::GetWorldOptions(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return {};

		READ_LOCK
		return m_worldDirectory.FindWorldOptions(worldId);
	}

	void ServerNetworkManager::EnumerateConnectedUsers(const std::function<void(uint64)>& fn)
	{
		if (!fn)
			return;

		std::vector<uint64> users;
		{
			READ_LOCK
			users.reserve(m_tcpSessions.size());
			for (const auto& uid : m_tcpSessions | std::views::keys)
				users.push_back(uid);
		}

		for (uint64 uid : users)
			fn(uid);
	}

	void ServerNetworkManager::EnumerateWorldUsers(WorldId worldId, const std::function<void(uint64)>& fn)
	{
		if (!fn || worldId == INVALID_WORLD_ID)
			return;

		std::vector<uint64> users;
		{
			READ_LOCK
			auto it = m_worldMembers.find(worldId);
			if (it == m_worldMembers.end())
				return;

			users = it->second;
		}

		for (uint64 uid : users)
			fn(uid);
	}

	void ServerNetworkManager::TryDestroyWorldIfEmpty(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return;

		const uint32 memberCount = GetWorldMemberCount(worldId);
		if (memberCount != 0)
			return;

		const WorldOptions options = GetWorldOptions(worldId);
		if (!options.destroyWhenEmpty || options.persistent)
			return;

		DestroyWorld(worldId);
	}

	void ServerNetworkManager::TryDestroyWorldIfEmpty(const WorldKey& key)
	{
		TryDestroyWorldIfEmpty(ResolveWorldId(key));
	}

	bool ServerNetworkManager::StartServerService()
	{
		ServiceConfig cfg{};
		cfg.localTcpAddress		= m_config.tcpAddress;
		cfg.localUdpAddress		= m_config.udpAddress;
		cfg.maxTcpSessionCount	= m_config.maxConnections;
		cfg.maxUdpSessionCount	= m_config.maxConnections;

		m_service = std::make_shared<ServerService>(cfg);
		if (!m_service)
			return false;

		m_service->SetSessionFactory<ServerTcpSession, ServerUdpSession>();
		m_service->SetSessionInitCallback([this](const std::shared_ptr<Session>& session)
			{
				if (auto tcp = dynamic_pointer_cast<ServerTcpSession>(session))
				{
					tcp->SetNetworkManager(this);
					RPCRegisterRequest<fb::fbTcpBindReqT>(tcp, tcp.get(), &ServerTcpSession::OnTcpBindRequest);
				}
				else if (auto udp = dynamic_pointer_cast<ServerUdpSession>(session))
				{
					udp->SetNetworkManager(this);
					RPCRegisterRequest<fb::fbUdpBindReqT>(udp, udp.get(), &ServerUdpSession::OnUdpBindRequest);
					RPCRegisterRequest<fb::fbRequestWorldAssignmentReqT>(udp, udp.get(), &ServerUdpSession::OnRequestWorldAssignmentReq);
					RPCRegisterRequest<fb::fbSpawnActorReqT>(udp, udp.get(), &ServerUdpSession::OnSpawnActorRequest);
					RPCRegisterRequest<fb::fbDespawnActorReqT>(udp, udp.get(), &ServerUdpSession::OnDespawnActorRequest);
					RPCRegisterRequest<fb::fbPossessActorReqT>(udp, udp.get(), &ServerUdpSession::OnPossessActorRequest);
					RPCRegisterRequest<fb::fbUnpossessActorReqT>(udp, udp.get(), &ServerUdpSession::OnUnpossessActorRequest);
				}
			});

		m_service->Init();

		if (!m_service->Start())
			return false;

		return true;
	}

	void ServerNetworkManager::StopServerService()
	{
		if (m_service)
		{
			m_service->CloseService();
			m_service.reset();
		}
	}
}
