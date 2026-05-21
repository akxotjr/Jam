#include "pch.h"
#include "jamnet/runtime/ClientNetworkManager.h"

#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardInvoke.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Service.h"

#include "jamnet/sync/replication/ReplicationTypes.h"


#include "jamnet/runtime/ClientSession.h"
#include "jamnet/runtime/AppRuntimeEvents.h"
#include "jamnet/runtime/world/ClientWorldActionSystem.h"

namespace jam::net
{
	namespace
	{
		inline const RouteDomain kClientPrincipalRouteDomain = RouteDomain::From("ClientPrincipal");
	}

	ClientNetworkManager::ClientNetworkManager(const ClientConfig& config, AccountId accountId)
		: m_config(config), m_accountId(accountId)
	{
		m_worldActionSystem = std::make_unique<ClientWorldActionSystem>();
		m_worldActionSystem->Init(this);
	}

	ClientNetworkManager::~ClientNetworkManager()
	{
		if (m_worldActionSystem)
			m_worldActionSystem->Shutdown();
		Disconnect();
	}

	bool ClientNetworkManager::Connect()
	{
		if (m_running.load(std::memory_order_acquire))
			return true;

		if (GetAccountId() == kInvalidAccountId)
		{
			JAMNET_LOG_ERROR_LOC("AccountId is not set");
			return false;
		}

		if (!StartClientService()) return false;

		m_running.store(true, std::memory_order_release);

		if (!ConnectTcp())
		{
			Disconnect();
			return false;
		}

		PublishNetworkStateEvent();

		return true;
	}

	void ClientNetworkManager::Disconnect()
	{
		const bool wasRunning = m_running.exchange(false, std::memory_order_acq_rel);
		const bool hasLiveState = wasRunning || m_service || m_tcp.TryGetRaw() || m_udp.TryGetRaw();
		if (!hasLiveState)
			return;

		m_tcpBound.store(false, std::memory_order_release);
		m_udpBound.store(false, std::memory_order_release);

		const bool wasReady = m_sessionReady.exchange(false, std::memory_order_acq_rel);

		(void)wasReady;

		if (auto* udp = m_udp.TryGetRaw())
		{
			udp->Disconnect();
			m_udp.Set(nullptr);
		}

		if (auto* tcp = m_tcp.TryGetRaw())
		{
			tcp->Disconnect();
			m_tcp.Set(nullptr);
		}

		SubmitUserContextUpdate([](UserContext& ctx)
			{
				ctx.tcp = kInvalidSessionId;
				ctx.udp = kInvalidSessionId;
			});

		PublishNetworkStateEvent();

		StopClientService();
	}

	bool ClientNetworkManager::IsConnected() const
	{
		return m_running.load(std::memory_order_acquire) && IsTcpConnected() && IsUdpConnected();
	}

	bool ClientNetworkManager::IsTcpConnected() const
	{
		if (auto* tcp = m_tcp.TryGetRaw())
			return tcp->IsConnected();
		return false;
	}

	bool ClientNetworkManager::IsUdpConnected() const
	{
		if (auto* udp = m_udp.TryGetRaw())
			return udp->IsConnected();
		return false;
	}


	void ClientNetworkManager::RequestWorldAction(eWorldAction action, const WorldKey& src, const WorldKey& target)
	{
		if (!m_worldActionSystem)
			return;

		WorldActionRequest req{};
		req.principalId = GetUserId();
		req.action = action;
		req.source = src;
		req.target = target;
		m_worldActionSystem->Execute(std::move(req));
	}


	void ClientNetworkManager::RequestSpawnActor(LocalWorldId worldId, const SpawnParams& params)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->RequestSpawnActor(worldId, params);
	}

	void ClientNetworkManager::RequestDespawnActor(LocalWorldId worldId, NetId netId)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->RequestDespawnActor(worldId, netId);
	}

	void ClientNetworkManager::RequestPossessActor(LocalWorldId worldId, NetId netId)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->RequestPossessActor(worldId, netId);
	}

	void ClientNetworkManager::RequestUnpossessActor(LocalWorldId worldId, NetId netId)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->RequestUnpossessActor(worldId, netId);
	}

	void ClientNetworkManager::PushInput(LocalWorldId worldId, uint32 inputFlags, float pitch, float yaw, uint32 commandEpoch)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->PushInput(worldId, inputFlags, pitch, yaw, commandEpoch);
	}

	void ClientNetworkManager::PushInput(LocalWorldId worldId, const px::CharacterInput& input)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->PushInput(worldId, input);
	}

	void ClientNetworkManager::SetLatestClickMoveSeq(LocalWorldId worldId, uint64 requestSeq)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->SetLatestClickMoveSeq(worldId, requestSeq);
	}

	void ClientNetworkManager::RequestClickMove(LocalWorldId worldId, const px::Vec3& from, const px::Vec3& dir, float maxRange, uint64 requestSeq, uint32 commandEpoch, float facingYaw)
	{
		if (m_worldActionSystem)
			m_worldActionSystem->RequestClickMove(worldId, from, dir, maxRange, requestSeq, commandEpoch, facingYaw);
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

		m_service->SetSessionInitCallback([this](Session* session)
			{
				if (!session)
					return;

				if (auto* tcp = dynamic_cast<ClientTcpSession*>(session))
					tcp->SetNetworkManager(this);
				else if (auto* udp = dynamic_cast<ClientUdpSession*>(session))
					udp->SetNetworkManager(this);

				session->SetAccountId(GetAccountId());
			});

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

		if (m_tcp.TryGetRaw())
			return true;

		auto sessionOwner = m_service->CreateTcpSession();
		if (!sessionOwner)
			return false;

		auto* session = static_cast<ClientTcpSession*>(sessionOwner.get());
		auto service = m_service;
		session->Submit(Job([service, session = std::move(sessionOwner)]() mutable
			{
				auto* raw = static_cast<ClientTcpSession*>(session.get());
				auto& state = GetOrCreateSessionShardState(CurrentShardLocalChecked());
				if (!state.AttachPreboundSession(std::unique_ptr<Session>(std::move(session))))
					return;

				service->NotifyTcpSessionAttached();
				raw->Connect();
			}, eJobPriority::Critical));

		m_tcp.Set(session);

		return true;
	}

	bool ClientNetworkManager::ConnectUdp()
	{
		if (!m_service)
			return false;

		if (m_udp.TryGetRaw())
			return true;

		auto sessionOwner = m_service->CreateUdpSession();
		if (!sessionOwner)
			return false;

		auto* session = static_cast<ClientUdpSession*>(sessionOwner.get());
		session->SetUserId(m_userId.load(std::memory_order_relaxed));

		auto service = m_service;
		session->Submit(Job([service, session = std::move(sessionOwner)]() mutable
			{
				auto* raw = static_cast<ClientUdpSession*>(session.get());
				auto& state = GetOrCreateSessionShardState(CurrentShardLocalChecked());
				const uint64 endpointId = raw->GetEndpointId();
				const RouteKey routeKey = raw->GetRouteKey();
				if (!state.AttachPreboundSession(std::unique_ptr<Session>(std::move(session))))
					return;

				service->NotifyUdpSessionAttached(endpointId, routeKey);
				raw->Connect();
			}, eJobPriority::Critical));

		m_udp.Set(session);

		return true;
	}

	void ClientNetworkManager::NotifyTcpBound(UserId userId)
	{
		m_tcp.Refresh();
		m_tcpBound.store(true, std::memory_order_release);
		m_userId.store(userId, std::memory_order_relaxed);

		UpdateSessionReadyState();

		if (!ConnectUdp())
			Disconnect();
	}

	void ClientNetworkManager::NotifyUdpBound(UserId userId)
	{
		if (m_userId.load(std::memory_order_relaxed) != userId)
			return;

		m_udp.Refresh();
		m_udpBound.store(true, std::memory_order_release);

		UpdateSessionReadyState();
	}





	void ClientNetworkManager::PublishNetworkStateEvent() const
	{
		NetworkStateEvent evt{};
		evt.accountId = GetAccountId();
		evt.userId = GetUserId();
		evt.state.phase = m_running.load(std::memory_order_acquire)
			? (m_sessionReady.load(std::memory_order_acquire) ? eNetworkPhase::Ready : eNetworkPhase::Connecting)
			: eNetworkPhase::Disconnected;
		GLOBAL_EVENTBUS_PUBLISH(evt);
	}

	void ClientNetworkManager::UpdateSessionReadyState()
	{
		const bool ready = m_tcpBound.load(std::memory_order_acquire) && m_udpBound.load(std::memory_order_acquire);
		const bool previous = m_sessionReady.exchange(ready, std::memory_order_acq_rel);

		if (auto* tcp = m_tcp.TryGetRaw()) tcp->SetReady(ready);
		if (auto* udp = m_udp.TryGetRaw()) udp->SetReady(ready);

		if (previous == ready)
			return;

		PublishNetworkStateEvent();
	}

	void ClientNetworkManager::SubmitUserContextUpdate(std::function<void(UserContext&)> fn)
	{
		if (!fn || m_accountId == kInvalidAccountId)
			return;

		const uint16 shardIndex = ResolveClientUserShardIndex();
		auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
		if (!shard)
			return;

		auto invoke = [accountId = m_accountId, fn = std::move(fn)](ShardLocal& local) mutable
			{
				auto& state = GetOrCreateUserShardState(local);
				if (auto* ctx = state.EnsureUserContext(accountId))
					fn(*ctx);
			};

		if (auto* local = CurrentShardLocal(); local && local->shardIndex == shardIndex)
		{
			invoke(*local);
			return;
		}

		shard->Submit(Job([invoke = std::move(invoke)]() mutable
			{
				invoke(CurrentShardLocalChecked());
			}, eJobPriority::Control));
	}



	uint16 ClientNetworkManager::ResolveClientUserShardIndex() const
	{
		if (m_accountId == kInvalidAccountId)
			return static_cast<uint16>(kInvalidRouteShard);

		const RouteKey routeKey = GLOBAL_EXEC.MakeRouteKey(kClientPrincipalRouteDomain, m_accountId);
		if (auto shard = GLOBAL_EXEC.GetShard(routeKey))
			return static_cast<uint16>(shard->GetIndex());

		return static_cast<uint16>(kInvalidRouteShard);
	}

}
