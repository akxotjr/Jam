#include "pch.h"
#include "jamnet/runtime/application/ServerNetworkManager.h"

#include "jamnet/core/utils/Clock.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/net/Service.h"

#include "jamnet/runtime/world/simulation/server/ServerWorld.h"

#include "jamnet/runtime/session/RuntimeShardRouting.h"
#include "jamnet/runtime/session/ServerSession.h"
#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/content/social/SocialService.h"
#include "jamnet/runtime/content/generic/GenericContentService.h"
#include "jamnet/runtime/world/lifecycle/ServerWorldTransitionCoordinator.h"
#include "jamnet/runtime/world/data/SharedDataManifestLoader.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/protocol/codec/WorldCodec.h"



namespace jam::net
{

	ServerNetworkManager::ServerNetworkManager(const ServerConfig& config)
		: m_config(config)
	{
		m_manifest = SharedDataManifestLoader::Load(config.sharedDataManifestPath);
		m_worldTransitions = std::make_shared<ServerWorldTransitionCoordinator>();
	}

	ServerNetworkManager::~ServerNetworkManager()
	{
		Stop();
	}

	std::unique_ptr<IWorldContent> ServerNetworkManager::CreateWorldContent(const WorldConfig& config) const
	{
		return m_config.worldContentFactory ? m_config.worldContentFactory(config) : nullptr;
	}

	std::optional<WorldArchetypeKey> ServerNetworkManager::ResolveEnterWorldDestination(AccountId accountId, UserId userId) const
	{
		return m_config.enterWorldDestinationResolver ? m_config.enterWorldDestinationResolver(accountId, userId) : std::nullopt;
	}

	bool ServerNetworkManager::Start()
	{
		if (m_running.load(std::memory_order_acquire))
			return true;

		if (!m_worldTransitions)
			m_worldTransitions = std::make_shared<ServerWorldTransitionCoordinator>();
		if (!m_worldTransitions->Initialize(this))
			return false;

		if (m_config.socialContent)
		{
			m_socialService = std::make_shared<SocialService>();
			if (!m_socialService->Initialize(this, m_config.socialContent))
			{
				m_socialService.reset();
				m_worldTransitions->Shutdown();
				return false;
			}
		}

		if (m_config.content)
		{
			m_contentService = std::make_shared<GenericContentService>();
			if (!m_contentService->Initialize(this, m_config.content))
			{
				m_contentService.reset();
				if (m_socialService)
					m_socialService->Shutdown();
				m_socialService.reset();
				m_worldTransitions->Shutdown();
				return false;
			}
		}

		if (!StartServerService())
		{
			if (m_contentService)
				m_contentService->Shutdown();
			m_contentService.reset();
			if (m_socialService)
				m_socialService->Shutdown();
			m_socialService.reset();
			m_worldTransitions->Shutdown();
			return false;
		}

		m_running.store(true, std::memory_order_release);
		StartWorldTransitionTicks();

		JAMNET_LOG_INFO("ServerNetworkManager started successfully");
		return true;
	}

	void ServerNetworkManager::Stop()
	{
		if (!m_running.exchange(false, std::memory_order_acq_rel))
			return;

		StopWorldTransitionTicks();

		if (m_worldTransitions)
			m_worldTransitions->Shutdown();

		StopServerService();

		if (m_socialService)
			m_socialService->Shutdown();
		if (m_contentService)
			m_contentService->Shutdown();

		m_worldTransitions.reset();
		m_socialService.reset();
		m_contentService.reset();

		JAMNET_LOG_INFO("ServerNetworkManager stopped");
	}


	void ServerNetworkManager::EnterWorld(UserId userId, const EnterWorldRequest& request)
	{
		if (!m_worldTransitions)
		{
			SendToUser(userId, codec::MakeWorldTransitionResultPacket({
				.kind		= eWorldTransitionKind::Enter,
				.requestId	= request.requestId,
				.failure	= eWorldTransitionFailure::InvalidRequest
			}), eProtocolType::TCP);

			return;
		}

		m_worldTransitions->Enter(userId, request, NOW_NS());
	}

	void ServerNetworkManager::LeaveWorld(UserId userId, const LeaveWorldRequest& request)
	{
		if (!m_worldTransitions)
		{
			SendToUser(userId, codec::MakeWorldTransitionResultPacket({
				.kind		= eWorldTransitionKind::Leave,
				.requestId	= request.requestId,
				.failure	= eWorldTransitionFailure::InvalidRequest
			}), eProtocolType::TCP);

			return;
		}

		m_worldTransitions->Leave(userId, request);
	}

	bool ServerNetworkManager::DispatchSocialCommand(UserId userId, SocialCommand command)
	{
		if (!m_socialService || userId == kInvalidUserId)
			return false;

		auto* local = CurrentShardLocal();
		if (!local || local->shardIndex != GetUserShardIndex(userId))
			return false;

		const auto& state = GetOrCreateUserShardState(*local);
		const UserContext* user = state.FindUserContext(userId);
		if (!user || user->tcp == kInvalidSessionId)
			return false;

		const SocialPrincipal principal{
			.accountId = user->accountId,
			.userId    = user->userId,
			.world     = user->worldState,
		};
		return m_socialService->Submit(principal, std::move(command));
	}

	bool ServerNetworkManager::DispatchContentRequest(UserId userId, GenericContentRequest request)
	{
		if (!m_contentService || userId == kInvalidUserId || !request.IsValid())
			return false;

		auto* local = CurrentShardLocal();
		if (!local || local->shardIndex != GetUserShardIndex(userId))
			return false;

		const auto& state = GetOrCreateUserShardState(*local);
		const UserContext* user = state.FindUserContext(userId);
		if (!user || user->tcp == kInvalidSessionId || user->connectionState != eUserConnectionState::Connected)
			return false;

		return m_contentService->Submit(GenericContentPrincipal{
			.accountId = user->accountId,
			.userId = user->userId,
		}, std::move(request));
	}

	void ServerNetworkManager::Authenticate(LoginCredential credential, AuthenticationCompleted completed) const
	{
		if (!completed)
			return;
		if (!m_config.authenticationContent)
		{
			completed(kInvalidAccountId);
			return;
		}
		m_config.authenticationContent->Authenticate(std::move(credential), std::move(completed));
	}

	void ServerNetworkManager::BootstrapWorldInstances(std::function<void(bool)> completed)
	{
		if (!m_worldTransitions)
		{
			if (completed) completed(false);
			return;
		}
		m_worldTransitions->BootstrapConfiguredWorlds(std::move(completed));
	}

	void ServerNetworkManager::DestroyWorld(const WorldRef& world)
	{
		if (m_worldTransitions)
			m_worldTransitions->DestroyWorld(world);
	}

	void ServerNetworkManager::ReleaseSession(UserId userId, const ServerTcpSession* tcp)
	{
		if (userId == kInvalidUserId || !tcp)
			return;

		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* user = userState.FindUserContext(userId);
		if (!user || user->tcp != tcp->GetSessionId())
			return;

		const ServerSessionBundle sessions = ResolveUserSessionBundle(*user);
		const SessionRef<ServerUdpSession> udpRef = sessions.udp;
		udpRef.TryPost(Job([udpRef]()
			{
				if (auto* udp = udpRef.TryGet(); udp && !udp->IsClosing())
					udp->Disconnect();
			}, eJobPriority::Control));

		user->tcp = kInvalidSessionId;
		user->udp = kInvalidSessionId;

		if (m_worldTransitions)
			m_worldTransitions->OnDisconnected(userId);
	}

	void ServerNetworkManager::NotifyUserConnected(const UserContext& user)
	{
		if (!m_socialService || user.accountId == kInvalidAccountId || user.userId == kInvalidUserId)
			return;

		m_socialService->NotifyConnected(SocialPrincipal{
			.accountId = user.accountId,
			.userId    = user.userId,
			.world     = user.worldState,
		});
	}

	void ServerNetworkManager::NotifyUserReleased(UserId userId)
	{
		if (m_socialService)
			m_socialService->NotifyDisconnected(userId);
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

	void ServerNetworkManager::StartWorldTransitionTicks()
	{
		StopWorldTransitionTicks();
		const std::weak_ptr<ServerWorldTransitionCoordinator> coordinator = m_worldTransitions;
		for (const auto& shard : GLOBAL_EXEC.GetShards())
		{
			if (!shard)
				continue;
			const PeriodicHandle handle = shard->ScheduleFixedDelay(
				Job([coordinator]()
					{
						if (const auto locked = coordinator.lock())
							locked->Tick(NOW_NS());
					}, eJobPriority::Control),
				{ .period_ns = 100_ms, .initialDelay_ns = 100_ms, .name = "WorldTransition.Tick" });
			m_worldTransitionTicks.emplace_back(shard, handle);
		}
	}

	void ServerNetworkManager::StopWorldTransitionTicks()
	{
		for (auto& [shard, handle] : m_worldTransitionTicks)
			if (shard)
				shard->CancelPeriodic(handle);
		m_worldTransitionTicks.clear();
	}
}
