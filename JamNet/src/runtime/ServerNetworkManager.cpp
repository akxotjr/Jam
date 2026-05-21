#include "pch.h"
#include "jamnet/runtime/ServerNetworkManager.h"

#include "jamnet/core/net/Service.h"

#include "jamnet/sync/networld/ServerPhysicalWorld.h"
#include "jamnet/sync/replication/ReplicationTypes.h"

#include "jamnet/runtime/ServerSession.h"
#include "jamnet/runtime/world/DefaultWorldAssignmentPolicy.h"
#include "jamnet/runtime/world/ServerWorldActionSystem.h"



namespace jam::net
{

	ServerNetworkManager::ServerNetworkManager(const ServerConfig& config)
		: m_config(config)
	{
		m_worldActionSystem = std::make_unique<ServerWorldActionSystem>();
	}

	ServerNetworkManager::~ServerNetworkManager()
	{
		Stop();
	}

	bool ServerNetworkManager::Start()
	{
		if (m_running.load(std::memory_order_acquire))
			return true;

		m_worldActionSystem->Init(this);

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

		if (m_worldActionSystem)
			m_worldActionSystem->Shutdown();

		{
			WRITE_LOCK_IDX(kSessionLockIdx)
			m_sessions.clear();
		}

		StopServerService();

		if (m_worldActionSystem)
		{
			m_worldActionSystem->Init(nullptr);
			m_worldActionSystem.reset();
		}

		JAMNET_LOG_INFO("ServerNetworkManager stopped");
	}


	void ServerNetworkManager::RequestWorldAction(WorldActionRequest req)
	{
		if (!m_worldActionSystem)
		{
			if (req.onResponse)
				req.onResponse({});
			return;
		}

		m_worldActionSystem->Execute(std::move(req));
	}

	void ServerNetworkManager::CreateWorld(WorldKey key)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->CreateWorld(key);
	}

	void ServerNetworkManager::DestroyWorld(const WorldKey& key)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->DestroyWorld(key);
	}

	bool ServerNetworkManager::SubmitWorldJob(const WorldKey& key, std::function<void(WorldBase&)> job)
	{
		return m_worldActionSystem ? m_worldActionSystem->SubmitWorldJob(key, std::move(job)) : false;
	}

	bool ServerNetworkManager::CacheTcpSession(UserId userId, ServerTcpSession* tcp)
	{
		if (userId == kInvalidUserId || !tcp)
			return false;

		{
			WRITE_LOCK_IDX(kSessionLockIdx);

			auto& bundle = m_sessions[userId];
			if (bundle.HasTcp() && bundle.TryGetTcp() != tcp)
			{
				JAMNET_LOG_WARN("TCP session is already registered.");
				return false;
			}

			bundle.tcp.Set(tcp);
		}

		if (m_worldActionSystem)
			m_worldActionSystem->OnSessionBundleChanged(userId);

		return true;
	}

	bool ServerNetworkManager::CacheUdpSession(UserId userId, ServerUdpSession* udp)
	{
		if (userId == kInvalidUserId || !udp)
			return false;

		{
			WRITE_LOCK_IDX(kSessionLockIdx);

			if (const auto it = m_sessions.find(userId); it != m_sessions.end())
			{
				if (it->second.HasTcp())
				{
					if (it->second.HasUdp() && it->second.TryGetUdp() != udp)
					{
						JAMNET_LOG_WARN("UDP session is already registered.");
						return false;
					}

					it->second.udp.Set(udp);
				}
				else
				{
					JAMNET_LOG_WARN("registered TCP session is null");
					return false;
				}
			}
			else
			{
				JAMNET_LOG_WARN("TCP session must be registered in the SessionBundle before UDP session is registered.");
				return false;
			}
		}

		if (m_worldActionSystem)
			m_worldActionSystem->OnSessionBundleChanged(userId);

		return true;
	}

	void ServerNetworkManager::ReleaseUdpSession(UserId userId, const ServerUdpSession* udp)
	{
		if (userId == kInvalidUserId || !udp)
			return;

		{
			WRITE_LOCK_IDX(kSessionLockIdx);
			if (auto it = m_sessions.find(userId); it != m_sessions.end())
			{
				if (it->second.udp.TryGet() == udp)
					it->second.udp.Set(nullptr);
			}
		}

		if (m_worldActionSystem)
			m_worldActionSystem->OnSessionBundleChanged(userId);
	}

	void ServerNetworkManager::ReleaseSession(UserId userId)
	{
		if (userId == kInvalidUserId) return;

		{
			WRITE_LOCK_IDX(kSessionLockIdx);
			m_sessions.erase(userId);
		}

		if (m_worldActionSystem)
			m_worldActionSystem->OnSessionUnregistered(userId);
	}

	ServerTcpSession* ServerNetworkManager::FindTcpSession(UserId userId)
	{
		READ_LOCK_IDX(kSessionLockIdx);

		const auto it = m_sessions.find(userId);
		return (it != m_sessions.end()) ? it->second.tcp.TryGet() : nullptr;
	}

	ServerUdpSession* ServerNetworkManager::FindUdpSession(UserId userId)
	{
		READ_LOCK_IDX(kSessionLockIdx);

		const auto it = m_sessions.find(userId);
		return (it != m_sessions.end()) ? it->second.udp.TryGet() : nullptr;
	}

	ServerSessionBundle ServerNetworkManager::GetSessionBundle(UserId userId)
	{
		READ_LOCK_IDX(kSessionLockIdx);

		const auto it = m_sessions.find(userId);
		return (it != m_sessions.end()) ? it->second : ServerSessionBundle{};
	}

	void ServerNetworkManager::Send(UserId userId, Packet packet, eProtocolType protocol)
	{
		if (!packet.IsValid()) return;

		Session* session = nullptr;

		if (protocol == eProtocolType::TCP)
		{
			session = FindTcpSession(userId);
			if (session && session->IsConnected())
				session->Send(packet);
			return;
		}

		if (protocol == eProtocolType::UDP)
		{
			session = FindUdpSession(userId);
			if (session && session->IsConnected())
				session->Send(packet);
			return;
		}

		JAMNET_LOG_WARN("Protocol is none");
	}

	void ServerNetworkManager::Multicast(const WorldKey& key, Packet packet)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->Multicast(key, std::move(packet));
	}

	void ServerNetworkManager::Broadcast(Packet packet)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->Broadcast(std::move(packet));
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
		m_service->SetSessionInitCallback([this](Session* session)
			{
				if (auto tcp = dynamic_cast<ServerTcpSession*>(session))
				{
					tcp->SetNetworkManager(this);
				}
				else if (auto udp = dynamic_cast<ServerUdpSession*>(session))
				{
					udp->SetNetworkManager(this);
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
