#include "pch.h"

#include "jamnet/runtime/ClientNetworkManager.h"
#include "jamnet/runtime/ClientSession.h"
#include "jamnet/runtime/ClientTransportAdapter.h"

#include "jamnet/sync/networld/ClientNetWorld.h"


namespace jam::net
{
	ClientNetworkManager::ClientNetworkManager(const ClientConfig& config, uint64 userId)
		: m_config(config)
	{
		m_world				= std::make_shared<ClientNetWorld>();
		m_transportAdapter  = std::make_shared<ClientTransportAdapter>();
		SetUserId(userId);
	}

	ClientNetworkManager::~ClientNetworkManager()
	{
		Disconnect();
	}

	bool ClientNetworkManager::Connect()
	{
		if (m_running.load(std::memory_order_acquire))
			return true;

		if (m_userId == 0)
		{
			JAMNET_LOG_ERROR_LOC("UserId is not set");
			return false;
		}

		if (!StartClientService()) return false;

		if (!ConnectTcp())
		{
			StopClientService();
			return false;
		}

		if (!ConnectUdp())
		{
			StopClientService();
			return false;
		}

		m_running.store(true, std::memory_order_release);
		InitializeWorldSkeleton();
		
		return true;
	}

	void ClientNetworkManager::Disconnect()
	{
		if (!m_running.exchange(false, std::memory_order_acq_rel))
			return;

		m_tcpBound.store(false, std::memory_order_release);
		m_udpBound.store(false, std::memory_order_release);
		m_matchmakingInFlight.store(false, std::memory_order_release);
		m_worldRunning.store(false, std::memory_order_release);
		m_groupId.store(0, std::memory_order_release);

		if (m_world)
			m_world->Stop();

		if (m_udp)
		{
			m_udp->Disconnect();
			m_udp.reset();
		}

		if (m_tcp)
		{
			m_tcp->Disconnect();
			m_tcp.reset();
		}

		StopClientService();
	}

	bool ClientNetworkManager::IsConnected() const
	{
		return m_running.load(std::memory_order_acquire) && IsTcpConnected() && IsUdpConnected();
	}

	bool ClientNetworkManager::IsTcpConnected() const
	{
		return m_tcp && m_tcp->IsConnected();
	}

	bool ClientNetworkManager::IsUdpConnected() const
	{
		return m_udp && m_udp->IsConnected();
	}

	void ClientNetworkManager::SetUserId(uint64 userId)
	{
		m_userId = userId;
		if (m_world) m_world->SetUserId(userId);
	}

	void ClientNetworkManager::TryMatchmaking()
	{
		if (!m_running.load(std::memory_order_acquire))
			return;

		if (!m_tcpBound.load(std::memory_order_acquire) || !m_udpBound.load(std::memory_order_acquire))
			return;

		if (!m_udp) return;

		if (m_groupId.load(std::memory_order_acquire) != 0)
			return;

		bool expected = false;
		if (!m_matchmakingInFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			return;

		m_udp->RequestGroupId(); 
	}


	bool ClientNetworkManager::StartClientService()
	{
		ServiceConfig cfg{};
		cfg.remoteTcpAddress = m_config.serverTcpAddress;
		cfg.remoteUdpAddress = m_config.serverUdpAddress;
		cfg.maxTcpSessionCount = 1;
		cfg.maxUdpSessionCount = 1;

		m_service = std::make_shared<ClientService>(cfg);
		if (!m_service)
			return false;

		if (!m_service->SetSessionFactory<ClientTcpSession, ClientUdpSession>())
			return false;

		m_service->Init();

		if (!m_service->Start())
			return false;

		return true;
	}

	void ClientNetworkManager::StopClientService()
	{
		if (m_service)
		{
			m_service->CloseService();
			m_service.reset();
		}
	}

	bool ClientNetworkManager::ConnectTcp()
	{
		if (!m_service)
			return false;

		if (m_tcp)
			return true;

		auto session = std::static_pointer_cast<ClientTcpSession>(m_service->CreateSession(eProtocolType::TCP));

		if (!session)
			return false;

		session->SetNetworkManager(this);
		session->SetUserId(m_userId);
		
		if (!session->Connect())
			return false;

		m_tcp = session;

		if (m_transportAdapter) m_transportAdapter->SetTcpSession(session);

		return true;
	}

	bool ClientNetworkManager::ConnectUdp()
	{
		if (!m_service)
			return false;

		if (m_udp)
			return true;

		auto session = std::static_pointer_cast<ClientUdpSession>(m_service->CreateSession(eProtocolType::UDP));

		if (!session)
			return false;

		session->SetNetworkManager(this);
		session->SetUserId(m_userId);

		if (!session->Connect())
			return false;

		m_udp = session;

		if (m_transportAdapter) m_transportAdapter->SetUdpSession(session);

		return true;
	}

	void ClientNetworkManager::InitializeWorldSkeleton() const
	{
		if (!m_world)
			return;

		if (m_transportAdapter) m_world->SetTransportSystem(m_transportAdapter);

		if (m_config.physicsFactory)
		{
			if (auto phys = m_config.physicsFactory())
				m_world->SetPhysicsFacade(std::move(phys));
		}

		m_world->SetLevelPath(m_config.levelPath);

		m_world->Init();
	}

	void ClientNetworkManager::StartWorld(uint32 groupId)
	{
		if (!m_world || groupId == 0)
			return;

		bool expected = false;
		if (!m_worldRunning.compare_exchange_strong(expected, true, std::memory_order_acquire))
			return;

		m_world->SetGroupId(groupId);
		m_world->Tick(SIMULATION_TICK_NS);
	}

	void ClientNetworkManager::NotifyTcpBound()
	{
		m_tcpBound.store(true, std::memory_order_release);
		UpdateSessionReadyState();
	}

	void ClientNetworkManager::NotifyUdpBound()
	{
		m_udpBound.store(true, std::memory_order_release);
		UpdateSessionReadyState();
		// TEMP: bind 완료 직후 자동 매치메이킹 트리거
		// ui trigger 로 변경 필요
		TryMatchmaking();
	}

	void ClientNetworkManager::NotifyMatchmakingSuccess(uint32 groupId)
	{
		if (groupId == 0)
		{
			m_matchmakingInFlight.store(false, std::memory_order_release);
			return;
		}

		// 이미 값이 있으면 무시
		uint32 expected = 0;
		if (!m_groupId.compare_exchange_strong(expected, groupId, std::memory_order_acq_rel))
		{
			m_matchmakingInFlight.store(false, std::memory_order_release);
			return;
		}

		m_matchmakingInFlight.store(false, std::memory_order_release);

		StartWorld(groupId);

		MatchmakingSucceededEvent evt{};
		evt.userId	= m_userId;
		evt.groupId = groupId;
		GLOBAL_EVENTBUS_PUBLISH(evt);
	}

	void ClientNetworkManager::UpdateSessionReadyState()
	{
		const bool ready = m_tcpBound.load(std::memory_order_acquire) && m_udpBound.load(std::memory_order_acquire);

		if (m_tcp) m_tcp->SetReady(ready);
		if (m_udp) m_udp->SetReady(ready);
	}
}
