#include "pch.h"

#include "jamnet/runtime/ClientNetworkManager.h"

#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/executor/Mailbox.h"
#include "jamnet/core/net/Service.h"

#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jamnet/sync/replication/ReplicationTypes.h"

#include "jamnet/runtime/ClientSession.h"
#include "jamnet/runtime/ClientTransportAdapter.h"

namespace jam::net
{
	ClientNetworkManager::ClientNetworkManager(const ClientConfig& config, uint64 userId)
		: m_config(config)
	{
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

		m_running.store(true, std::memory_order_release);

		if (!ConnectTcp())
		{
			Disconnect();
			return false;
		}

		InitializeWorldSkeleton();
		
		return true;
	}

	void ClientNetworkManager::Disconnect()
	{
		const bool wasRunning = m_running.exchange(false, std::memory_order_acq_rel);
		const bool hasLiveState = wasRunning || m_service || m_tcp.TryGet() || m_udp.TryGet() || m_world;
		if (!hasLiveState)
			return;

		m_tcpBound.store(false, std::memory_order_release);
		m_udpBound.store(false, std::memory_order_release);
		m_worldAssignmentInFlight.store(false, std::memory_order_release);
		m_worldRunning.store(false, std::memory_order_release);
		m_worldId.store(INVALID_WORLD_ID, std::memory_order_release);

		const bool wasReady = m_sessionReady.exchange(false, std::memory_order_acq_rel);
		{
			std::scoped_lock lock(m_lastWorldRequestLock);
			m_lastWorldRequestResult.reset();
		}

		if (wasReady)
		{
			ClientSessionReadyEvent evt{};
			evt.userId = m_userId;
			evt.tcpBound = false;
			evt.udpBound = false;
			evt.ready    = false;
			GLOBAL_EVENTBUS_PUBLISH(evt);
		}

		PublishBindStateChangedEvent();

		ResetWorldInstance();

		if (auto* udp = m_udp.TryGet())
		{
			udp->Disconnect();
			m_udp.Set(nullptr);
		}

		if (auto* tcp = m_tcp.TryGet())
		{
			tcp->Disconnect();
			m_tcp.Set(nullptr);
		}

		StopClientService();
	}

	bool ClientNetworkManager::IsConnected() const
	{
		return m_running.load(std::memory_order_acquire) && IsTcpConnected() && IsUdpConnected();
	}

	bool ClientNetworkManager::IsTcpConnected() const
	{
		if (auto* tcp = m_tcp.TryGet())
			return tcp->IsConnected();
		return false;
	}

	bool ClientNetworkManager::IsUdpConnected() const
	{
		if (auto* udp = m_udp.TryGet())
			return udp->IsConnected();
		return false;
	}

	void ClientNetworkManager::SetUserId(uint64 userId)
	{
		m_userId = userId;
		if (m_world)
			m_world->SetUserId(userId);
	}

	bool ClientNetworkManager::RequestAutoAssignWorld()
	{
		return RequestWorldAction([](ClientUdpSession& session)
		{
			session.RequestAutoAssignWorld();
		});
	}

	bool ClientNetworkManager::RequestJoinWorld(const WorldKey& targetWorld)
	{
		return RequestWorldAction([&targetWorld](ClientUdpSession& session)
		{
			session.RequestJoinWorld(targetWorld);
		});
	}

	bool ClientNetworkManager::RequestLeaveWorld()
	{
		return RequestWorldAction([](ClientUdpSession& session)
		{
			session.RequestLeaveWorld();
		});
	}

	bool ClientNetworkManager::RequestTransferWorld(const WorldKey& targetWorld)
	{
		return RequestWorldAction([&targetWorld](ClientUdpSession& session)
		{
			session.RequestTransferWorld(targetWorld);
		});
	}

	ClientBindState ClientNetworkManager::GetBindState() const
	{
		ClientBindState state{};
		state.tcpBound = m_tcpBound.load(std::memory_order_acquire);
		state.udpBound = m_udpBound.load(std::memory_order_acquire);
		state.ready	   = m_sessionReady.load(std::memory_order_acquire);
		return state;
	}

	std::optional<WorldRequestResultEvent> ClientNetworkManager::GetLastWorldRequestResult() const
	{
		std::scoped_lock lock(m_lastWorldRequestLock);
		return m_lastWorldRequestResult;
	}


	bool ClientNetworkManager::StartClientService()
	{
		ServiceConfig cfg{};
		cfg.remoteTcpAddress   = m_config.serverTcpAddress;
		cfg.remoteUdpAddress   = m_config.serverUdpAddress;
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

		if (m_tcp.TryGet())
			return true;

		auto* session = static_cast<ClientTcpSession*>(m_service->CreateTcpSession());

		if (!session)
			return false;

		session->SetNetworkManager(this);
		session->SetUserId(m_userId);
		
		if (!session->Connect())
			return false;

		m_tcp.Set(session);

		if (m_transportAdapter) m_transportAdapter->SetTcpSession(session);

		return true;
	}

	bool ClientNetworkManager::ConnectUdp()
	{
		if (!m_service)
			return false;

		if (m_udp.TryGet())
			return true;

		auto* session = static_cast<ClientUdpSession*>(m_service->CreateUdpSession());

		if (!session)
			return false;

		session->SetNetworkManager(this);
		session->SetUserId(m_userId);

		if (!session->Connect())
			return false;

		m_udp.Set(session);

		if (m_transportAdapter) m_transportAdapter->SetUdpSession(session);

		return true;
	}

	void ClientNetworkManager::InitializeWorldSkeleton()
	{
		if (!m_world)
			m_world = std::make_shared<ClientNetWorld>();

		m_world->SetUserId(m_userId);

		if (m_transportAdapter)
			m_world->SetTransportSystem(m_transportAdapter);

		m_world->SetHeadless(m_config.headlessWorld);

		if (!m_config.headlessWorld && m_config.physicsFactory)
		{
			if (auto phys = m_config.physicsFactory())
				m_world->SetPhysicsFacade(std::move(phys));
		}

		m_world->SetLevelPath(m_config.levelPath);

		m_world->Init();
	}

	bool ClientNetworkManager::RequestWorldAction(const std::function<void(ClientUdpSession&)>& issueRequest)
	{
		if (!m_running.load(std::memory_order_acquire))
			return false;

		if (!m_tcpBound.load(std::memory_order_acquire) || !m_udpBound.load(std::memory_order_acquire))
			return false;

		auto* udp = m_udp.TryGet();
		if (!udp)
			return false;

		bool expected = false;
		if (!m_worldAssignmentInFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			return false;

		issueRequest(*udp);
		return true;
	}

	void ClientNetworkManager::StartWorld(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return;

		bool expected = false;
		if (!m_worldRunning.compare_exchange_strong(expected, true, std::memory_order_acquire))
			return;

		if (!m_world)
			InitializeWorldSkeleton();

		if (!m_world)
		{
			m_worldRunning.store(false, std::memory_order_release);
			return;
		}

		m_world->SetWorldId(worldId);
		m_world->Tick(SIMULATION_TICK_NS);
	}

	void ClientNetworkManager::StopWorld()
	{
		m_worldRunning.store(false, std::memory_order_release);
		m_worldId.store(INVALID_WORLD_ID, std::memory_order_release);

		ResetWorldInstance();
	}

	void ClientNetworkManager::ResetWorldInstance()
	{
		auto world = std::move(m_world);
		if (!world)
			return;

		world->BeginShutdown(eMailboxCloseMode::Abort);
	}

	void ClientNetworkManager::NotifyTcpBound()
	{
		m_tcpBound.store(true, std::memory_order_release);

		ClientTcpBoundEvent evt{};
		evt.userId = m_userId;
		GLOBAL_EVENTBUS_PUBLISH(evt);

		if (auto* udp = m_udp.TryGet(); udp && udp->IsConnected() && !m_udpBound.load(std::memory_order_acquire))
			udp->RequestUdpBind();

		UpdateSessionReadyState();
		PublishBindStateChangedEvent();

		if (!ConnectUdp())
		{
			Disconnect();
		}
	}

	void ClientNetworkManager::NotifyUdpBound()
	{
		m_udpBound.store(true, std::memory_order_release);

		ClientUdpBoundEvent evt{};
		evt.userId = m_userId;
		GLOBAL_EVENTBUS_PUBLISH(evt);

		UpdateSessionReadyState();
		PublishBindStateChangedEvent();
	}

	void ClientNetworkManager::NotifyWorldRequestResult(uint8 status, uint8 requestAction, uint8 assignmentAction, uint8 reason, WorldId worldId)
	{
		WorldRequestResultEvent requestEvt{};
		requestEvt.userId			 = m_userId;
		requestEvt.status			 = ToWorldAssignmentStatus(status);
		requestEvt.requestAction	 = ToWorldRequestAction(requestAction);
		requestEvt.assignmentAction  = ToWorldAssignmentAction(assignmentAction);
		requestEvt.reason			 = ToWorldTransferReason(reason);
		requestEvt.worldId			 = worldId;
		{
			std::scoped_lock lock(m_lastWorldRequestLock);
			m_lastWorldRequestResult = requestEvt;
		}
		GLOBAL_EVENTBUS_PUBLISH(requestEvt);

		if (requestEvt.IsWaiting())
		{
			m_worldAssignmentInFlight.store(true, std::memory_order_release);
			return;
		}

		m_worldAssignmentInFlight.store(false, std::memory_order_release);

		if (!requestEvt.IsAssigned())
			return;

		if (requestEvt.requestAction == eWorldRequestAction::Leave)
		{
			StopWorld();
			return;
		}

		const WorldId currentWorldId = m_worldId.load(std::memory_order_acquire);
		if (worldId == INVALID_WORLD_ID)
		{
			if (currentWorldId != INVALID_WORLD_ID)
				StopWorld();
			return;
		}

		if (currentWorldId != INVALID_WORLD_ID && currentWorldId != worldId)
			StopWorld();

		m_worldId.store(worldId, std::memory_order_release);
		StartWorld(worldId);

		WorldAssignmentSucceededEvent evt{};
		evt.userId	= m_userId;
		evt.worldId = worldId;
		GLOBAL_EVENTBUS_PUBLISH(evt);
	}

	void ClientNetworkManager::PublishBindStateChangedEvent() const
	{
		ClientBindStateChangedEvent evt{};
		evt.userId = m_userId;
		evt.state  = GetBindState();
		GLOBAL_EVENTBUS_PUBLISH(evt);
	}

	void ClientNetworkManager::UpdateSessionReadyState()
	{
		const bool ready = m_tcpBound.load(std::memory_order_acquire) && m_udpBound.load(std::memory_order_acquire);
		const bool previous = m_sessionReady.exchange(ready, std::memory_order_acq_rel);

		if (auto* tcp = m_tcp.TryGet()) tcp->SetReady(ready);
		if (auto* udp = m_udp.TryGet()) udp->SetReady(ready);

		if (previous == ready)
			return;

		ClientSessionReadyEvent evt{};
		evt.userId	 = m_userId;
		evt.tcpBound = m_tcpBound.load(std::memory_order_acquire);
		evt.udpBound = m_udpBound.load(std::memory_order_acquire);
		evt.ready	 = ready;
		GLOBAL_EVENTBUS_PUBLISH(evt);
	}
}
