#include "pch.h"
#include "jamnet/runtime/application/ClientNetworkManager.h"

#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Service.h"

#include "jamnet/runtime/session/ClientSession.h"
#include "jamnet/runtime/application/AppRuntimeEvents.h"
#include "jamnet/runtime/world/actor/ActorArchetypesLoader.h"
#include "jamnet/runtime/world/data/SharedDataManifestLoader.h"
#include "jamnet/runtime/world/data/WorldTemplatesLoader.h"
#include "jamnet/runtime/world/data/WorldArchetypesLoader.h"
#include "jamnet/runtime/world/data/ActorLevelsLoader.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"
#include "jamnet/runtime/world/simulation/common/ReplicationTypes.h"

#include <atomic>
#include <future>
#include <vector>

namespace jam::net
{
	namespace
	{
		std::atomic<uint64> g_nextClientInstanceId = 1;
	}

	ClientNetworkManager::ClientNetworkManager(const ClientConfig& config)
		: m_config(config)
	{
		m_clientInstanceId = g_nextClientInstanceId.fetch_add(1, std::memory_order_relaxed);

		if (m_config.loginId.empty() && m_config.accountId != kInvalidAccountId)
			m_config.loginId = std::to_string(m_config.accountId);
		if (m_config.password.empty() && m_config.accountId != kInvalidAccountId)
			m_config.password = std::to_string(m_config.accountId);

		m_principal.accountId = config.accountId;
		const uint64 principalSeed = config.accountId != kInvalidAccountId
			? config.accountId : static_cast<uint64>(std::hash<std::string>{}(config.loginId));
		m_principalShard = GLOBAL_EXEC.GetAffinityShard(principalSeed);

		m_manifest			  = SharedDataManifestLoader::Load(config.sharedDataManifestPath);
		m_worldTemplates	  = WorldTemplatesLoader::Load(m_manifest.worldTemplateDatabasePath);
		m_worldArchetypes	  = WorldArchetypesLoader::Load(m_manifest.worldArchetypeDatabasePath);
		m_actorArchetypes	  = ActorArchetypesLoader::Load(m_manifest.actorArchetypeDatabasePath);
		m_worldConfigResolver = std::make_unique<WorldConfigResolver>(&m_manifest, &m_worldTemplates, &m_worldArchetypes);
	}

	ClientNetworkManager::~ClientNetworkManager()
	{
		ShutdownPrincipalAndWait();
	}

	bool ClientNetworkManager::Connect()
	{

		if (m_config.ticket.empty() && (m_config.loginId.empty() || m_config.password.empty()))
		{
			JAMNET_LOG_ERROR_LOC("Login credential is not set");
			return false;
		}
		if (m_config.loginId.size() > kMaxLoginIdBytes
			|| m_config.password.size() > kMaxLoginSecretBytes
			|| m_config.ticket.size() > kMaxLoginSecretBytes)
		{
			JAMNET_LOG_ERROR_LOC("Login credential exceeds the binding packet limit");
			return false;
		}
		if (!m_principalShard)
		{
			JAMNET_LOG_ERROR_LOC("Client principal affinity shard is unavailable");
			return false;
		}
		m_principalShard->Submit(Job(shared_from_this(), &ClientNetworkManager::ConnectOnPrincipalShard, eJobPriority::Control));

		return true;
	}

	void ClientNetworkManager::Disconnect()
	{
		ClearPendingCharacterControl();
		if (!m_principalShard)
			return;

		auto self = shared_from_this();
		m_principalShard->Submit(Job([self = std::move(self)]()
			{
				self->DisconnectOnPrincipalShard();
			}, eJobPriority::Control));
	}

	void ClientNetworkManager::Shutdown()
	{
		ShutdownPrincipalAndWait();
	}

	bool ClientNetworkManager::ConnectOnPrincipalShard()
	{
		AssertPrincipalAffinity();
		if (m_running.load(std::memory_order_acquire))
			return true;

		if (!StartClientService())
		{
			PublishNetworkStateEvent();
			return false;
		}

		m_running.store(true, std::memory_order_release);
		if (!ConnectTcp())
		{
			DisconnectOnPrincipalShard();
			return false;
		}

		PublishNetworkStateEvent();
		return true;
	}

	void ClientNetworkManager::DisconnectOnPrincipalShard(std::function<void()> completed)
	{
		AssertPrincipalAffinity();

		m_running.store(false, std::memory_order_relaxed);

		auto& mainWorld = m_principal.mainWorld;
		const auto current  = mainWorld.Current();
		const auto prepared = mainWorld.Prepared();
		const UserId userId = m_principal.userId;

		if (current && current->worldObject && userId != kInvalidUserId)
			current->worldObject->RemoveMember(userId);
		mainWorld.Clear();

		m_principal.udp = nullptr;
		m_principal.tcp = nullptr;
		m_principal.userId = kInvalidUserId;

		m_tcpBound.store(false, std::memory_order_release);
		m_udpBound.store(false, std::memory_order_release);
		m_sessionReady.store(false, std::memory_order_relaxed);
		m_bootstrapKind.store(eBootstrapKind::Pending, std::memory_order_release);

		PublishNetworkStateEvent();

		std::vector<std::shared_ptr<ClientWorld>> worlds;
		if (current && current->worldObject)
			worlds.push_back(current->worldObject);
		if (prepared && prepared->worldObject && (!current || current->worldObject != prepared->worldObject))
			worlds.push_back(prepared->worldObject);

		if (worlds.empty())
		{
			StopClientService(std::move(completed));
			return;
		}

		auto self = shared_from_this();
		auto remaining = std::make_shared<std::atomic_size_t>(worlds.size());
		auto finalize = [self = std::move(self), completed = std::move(completed), remaining]() mutable
			{
				if (remaining->fetch_sub(1, std::memory_order_acq_rel) != 1)
					return;

				self->StopClientService(std::move(completed));
			};

		for (const auto& world : worlds)
			world->Shutdown(eMailboxCloseMode::Abort, finalize);
	}

	void ClientNetworkManager::ShutdownPrincipalAndWait()
	{
		if (!m_principalShard)
			return;
		if (IsOnPrincipalShard())
		{
			DisconnectOnPrincipalShard();
			return;
		}

		auto completed = std::make_shared<std::promise<void>>();
		auto future = completed->get_future();
		m_principalShard->Submit(Job([this, completed]()
			{
				DisconnectOnPrincipalShard([completed]() { completed->set_value(); });
			}, eJobPriority::Control));
		future.wait();
	}

	bool ClientNetworkManager::IsConnected() const
	{
		return m_running.load(std::memory_order_acquire) && IsTcpConnected() && IsUdpConnected();
	}

	bool ClientNetworkManager::IsTcpConnected() const
	{
		AssertPrincipalAffinity();

		if (auto* tcp = m_principal.tcp)
			return tcp->IsConnected();
		return false;
	}

	bool ClientNetworkManager::IsUdpConnected() const
	{
		if (auto* udp = m_principal.udp)
			return udp->IsConnected();
		return false;
	}


	bool ClientNetworkManager::RequestWorldAction(const WorldActionCommand& command)
	{
		if (!m_principalShard)
			return false;

		m_principalShard->Submit(Job(shared_from_this(), &ClientNetworkManager::RequestWorldActionOnPrincipalShard, eJobPriority::Control, command));
		return true;
	}

	bool ClientNetworkManager::RequestWorldActionOnPrincipalShard(const WorldActionCommand& command)
	{
		AssertPrincipalAffinity();
		auto* tcp = m_principal.tcp;
		if (!tcp || !IsTcpConnected())
			return false;

		return std::visit([this, tcp]<typename ReqT>(const ReqT& request)
			{
				if (request.expectedMainRevision != m_principal.mainWorld.Revision())
					return false;
				if constexpr (std::is_same_v<ReqT, EnterWorldRequest>)
					return request.IsValid() && tcp->RequestEnterWorld(request);
				else
					return tcp->RequestLeaveWorld(request);
			}, command.payload);
	}

	bool ClientNetworkManager::RequestActorAction(const ActorActionCommand& command)
	{
		if (!m_principalShard)
			return false;

		if (IsOnPrincipalShard())
			return RequestActorActionOnPrincipalShard(command);

		m_principalShard->Submit(Job(shared_from_this(), &ClientNetworkManager::RequestActorActionOnPrincipalShard, eJobPriority::Control, command));
		return true;
	}

	bool ClientNetworkManager::RequestActorActionOnPrincipalShard(const ActorActionCommand& command)
	{
		AssertPrincipalAffinity();
		const auto [requestId, action] = std::visit([]<typename ReqT>(const ReqT& request)
			{
				if constexpr (std::is_same_v<ReqT, SpawnActorRequest>)
					return std::pair{ request.requestId, eActorAction::Spawn };
				else
					return std::pair{ request.requestId, eActorAction::Despawn };
			}, command.payload);
		if (requestId == kInvalidClientRequestId)
			return false;

		const auto current = m_principal.mainWorld.Current();
		if (!current || !current->worldObject)
		{
			PublishActorActionFailure(requestId, action, eActorActionReason::WorldUnavailable);
			return false;
		}

		const WorldEventCorrelation correlation
		{
			.world = current->world,
			.mainRevision = m_principal.mainWorld.Revision(),
		};
		current->worldObject->SubmitActorAction(command, correlation);
		return true;
	}

	bool ClientNetworkManager::RequestSocialCommand(const SocialCommand& command)
	{
		if (!m_principalShard)
			return false;

		m_principalShard->Submit(Job(shared_from_this(), &ClientNetworkManager::RequestSocialCommandOnPrincipalShard, eJobPriority::Control, command));
		return true;
	}

	bool ClientNetworkManager::RequestSocialCommandOnPrincipalShard(const SocialCommand& command)
	{
		AssertPrincipalAffinity();
		auto* tcp = m_principal.tcp;
		if (!tcp || !IsTcpConnected() || command.requestId == kInvalidClientRequestId)
			return false;
		return tcp->SendSocialCommand(command);
	}

	bool ClientNetworkManager::RequestGenericContent(const GenericContentRequest& request)
	{
		if (!m_principalShard)
			return false;

		m_principalShard->Submit(Job(shared_from_this(), &ClientNetworkManager::RequestGenericContentOnPrincipalShard, eJobPriority::Control, request));
		return true;
	}

	bool ClientNetworkManager::RequestGenericContentOnPrincipalShard(const GenericContentRequest& request)
	{
		AssertPrincipalAffinity();
		auto* tcp = m_principal.tcp;
		if (!tcp || !IsTcpConnected() || !request.IsValid())
			return false;
		return tcp->SendGenericContentRequest(request);
	}

	void ClientNetworkManager::PublishSocialMessage(SocialMessage message) const
	{
		AssertPrincipalAffinity();
		SocialMessageEvent event{
			.accountId = GetAccountId(),
			.userId = GetUserId(),
			.message = std::move(message),
		};
		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientNetworkManager::PublishGenericContentResponse(GenericContentResponse response) const
	{
		AssertPrincipalAffinity();
		GenericContentResponseEvent event{
			.accountId = GetAccountId(),
			.userId = GetUserId(),
			.response = std::move(response),
		};
		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientNetworkManager::PublishActorActionFailure(ClientRequestId requestId, eActorAction action, eActorActionReason reason) const
	{
		ActorActionResultEvent event{};
		event.accountId = GetAccountId();
		event.userId	= GetUserId();
		event.requestId = requestId;
		event.result	= ActorActionResult
		{
			.status = eActorActionStatus::Failed,
			.reason = reason,
			.action = action,
		};
		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientNetworkManager::SubmitCharacterControl(const CharacterControlIntent& intent)
	{
		if (!m_principalShard)
			return;

		bool scheduleDrain = false;
		{
			std::scoped_lock lock(m_characterControlMutex);
			const CharacterActionFlags pendingEdges = m_pendingCharacterControl ? m_pendingCharacterControl->edgeActions : 0;
			m_pendingCharacterControl = intent;
			m_pendingCharacterControl->edgeActions |= pendingEdges;
			if (!m_characterControlDrainPending)
			{
				m_characterControlDrainPending = true;
				scheduleDrain = true;
			}
		}

		if (scheduleDrain)
			m_principalShard->Submit(Job(shared_from_this(), &ClientNetworkManager::DrainCharacterControlOnPrincipalShard, eJobPriority::Normal));
	}

	void ClientNetworkManager::DrainCharacterControlOnPrincipalShard()
	{
		AssertPrincipalAffinity();

		std::optional<CharacterControlIntent> intent;
		{
			std::scoped_lock lock(m_characterControlMutex);
			intent = m_pendingCharacterControl;
			m_pendingCharacterControl.reset();
			m_characterControlDrainPending = false;
		}

		if (!intent)
			return;

		const auto current = m_principal.mainWorld.Current();
		if (!current || !current->worldObject)
			return;

		current->worldObject->SubmitCharacterControl(*intent);
	}

	void ClientNetworkManager::ClearPendingCharacterControl()
	{
		std::scoped_lock lock(m_characterControlMutex);
		m_pendingCharacterControl.reset();
	}

	bool ClientNetworkManager::StartClientService()
	{
		ServiceConfig cfg{};
		cfg.remoteTcpAddress   = m_config.serverTcpAddress;
		cfg.remoteUdpAddress   = m_config.serverUdpAddress;
		cfg.maxTcpSessionCount = 1;
		cfg.maxUdpSessionCount = 1;

		m_service = std::make_shared<ClientService>(cfg, GetAccountId(), m_principalShard);
		if (!m_service)
			return false;

		if (!m_service->SetSessionFactory<ClientTcpSession, ClientUdpSession>())
			return false;

		m_service->SetSessionInitCallback([this](Session* session)
			{
				if (!session)
					return;

				if (auto* tcp = dynamic_cast<ClientTcpSession*>(session))
				{
					tcp->SetNetworkManager(this);
					if (!m_config.ticket.empty())
						tcp->SetTicketCredential(m_config.ticket);
					else
						tcp->SetPasswordCredential(m_config.loginId, m_config.password);
				}
				else if (auto* udp = dynamic_cast<ClientUdpSession*>(session))
					udp->SetNetworkManager(this);

				session->SetAccountId(GetAccountId());
			});

		m_service->Init();

		if (!m_service->Start())
			return false;

		return true;
	}

	void ClientNetworkManager::StopClientService(std::function<void()> completed)
	{
		if (!m_service)
		{
			if (completed) completed();
			return;
		}

		auto service = m_service;
		auto self = shared_from_this();
		service->BeginClose([self = std::move(self), service = std::move(service), completed = std::move(completed)]() mutable
			{
				if (self->m_service == service)
					self->m_service.reset();
				if (completed)
					completed();
			});
	}

	bool ClientNetworkManager::ConnectTcp()
	{
		if (!m_service)
			return false;

		if (auto* session = static_cast<ClientTcpSession*>(m_service->FindTcpSession()))
		{
			m_principal.tcp = session;
			return true;
		}

		auto sessionOwner = m_service->CreateTcpSession();
		if (!sessionOwner)
			return false;

		auto* session = static_cast<ClientTcpSession*>(sessionOwner.get());
		if (!m_service->AttachTcpSession(std::move(sessionOwner)))
			return false;

		m_principal.tcp = session;
		session->Connect();

		return true;
	}

	bool ClientNetworkManager::ConnectUdp()
	{
		if (!m_service)
			return false;

		if (auto* session = static_cast<ClientUdpSession*>(m_service->FindUdpSession()))
		{
			m_principal.udp = session;
			return true;
		}

		auto sessionOwner = m_service->CreateUdpSession();
		if (!sessionOwner)
			return false;

		auto* session = static_cast<ClientUdpSession*>(sessionOwner.get());
		session->SetUserId(m_principal.userId);
		if (!m_service->AttachUdpSession(std::move(sessionOwner)))
			return false;

		m_principal.udp = session;
		session->Connect();

		return true;
	}

	void ClientNetworkManager::NotifyTcpBound(AccountId accountId, UserId userId)
	{
		AssertPrincipalAffinity();

		m_principal.accountId = accountId;
		if (m_service)
			m_service->SetAccountId(accountId);
		m_tcpBound.store(true, std::memory_order_release);
		m_principal.userId = userId;

		UpdateSessionReadyState();

		if (!ConnectUdp())
			DisconnectOnPrincipalShard();
	}

	void ClientNetworkManager::NotifyUdpBound(UserId userId)
	{
		AssertPrincipalAffinity();
		if (m_principal.userId != userId)
			return;

		m_udpBound.store(true, std::memory_order_release);

		UpdateSessionReadyState();
	}

	void ClientNetworkManager::NotifyBootstrap(UserId userId, eBootstrapKind kind)
	{
		AssertPrincipalAffinity();
		if (userId != m_principal.userId
			|| (kind != eBootstrapKind::Fresh && kind != eBootstrapKind::Resync))
		{
			JAMNET_LOG_WARN("[UserBootstrap] rejected. receivedUserId={}, principalUserId={}, kind={}",
				userId, m_principal.userId, static_cast<uint32>(kind));
			return;
		}

		m_bootstrapKind.store(kind, std::memory_order_release);
		JAMNET_LOG_INFO("[UserBootstrap] accepted. userId={}, kind={}, tcpBound={}, udpBound={}",
			userId, static_cast<uint32>(kind),
			m_tcpBound.load(std::memory_order_acquire), m_udpBound.load(std::memory_order_acquire));
		UpdateSessionReadyState();
	}

	void ClientNetworkManager::NotifyTcpDisconnected(const ClientTcpSession* session)
	{
		AssertPrincipalAffinity();
		if (m_principal.tcp != session)
			return;

		m_principal.tcp = nullptr;
		m_tcpBound.store(false, std::memory_order_release);
		m_bootstrapKind.store(eBootstrapKind::Pending, std::memory_order_release);
		UpdateSessionReadyState();
	}

	void ClientNetworkManager::NotifyUdpDisconnected(const ClientUdpSession* session)
	{
		AssertPrincipalAffinity();
		if (m_principal.udp != session)
			return;

		m_principal.udp = nullptr;
		m_udpBound.store(false, std::memory_order_release);
		UpdateSessionReadyState();
	}

	void ClientNetworkManager::PrepareMainWorld(const ClientWorldPrepare& prepare, std::function<void(bool)> completed)
	{
		AssertPrincipalAffinity();
		auto& mainWorld = m_principal.mainWorld;
		if (!prepare.token.IsValid() || !prepare.correlation.world.IsValid() || !m_worldConfigResolver)
		{
			if (completed) completed(false);
			return;
		}
		if (const auto& pending = mainWorld.Prepared(); pending && pending->syncToken == prepare.token)
		{
			if (completed) completed(pending->world == prepare.correlation.world);
			return;
		}
		if (const auto& current = mainWorld.Current(); current && current->world == prepare.correlation.world
			&& prepare.kind != eWorldSyncKind::WorldResync)
		{
			if (completed) completed(mainWorld.Prepare(prepare, current->mailbox, current->worldObject));
			return;
		}
		if (const auto pending = mainWorld.Prepared(); pending && pending->mailbox.IsValid())
		{
			const auto& current = mainWorld.Current();
			if ((!current || current->mailbox.ownerId != pending->mailbox.ownerId) && pending->worldObject)
				pending->worldObject->Shutdown(eMailboxCloseMode::Abort);

			mainWorld.Cancel(pending->syncToken);
		}

		WorldConfig config = m_worldConfigResolver->ResolveWorldConfig(prepare.correlation.world.instance);
		config.world.worldId = prepare.correlation.world.worldId;
		if (!config.IsValid())
		{
			if (completed) completed(false);
			return;
		}

		ClientWorldBinding binding;
		auto world = std::make_shared<ClientWorld>(config);
		world->SetPipelineSubtype(m_nextWorldPipelineSubtype++);
		world->SetPrincipalState(&m_principal);
		world->SetHeadless(m_config.headlessMode);

		if (world)
			world->SetActorArchetypeDatabase(m_actorArchetypes);
		if (world && !config.actorLevelPath.empty())
			world->SetActorLevelDatabase(ActorLevelsLoader::Load(config.actorLevelPath));

		if (world && world->Init())
			binding = { .world = config.GetWorldRef(), .mailbox = world->GetMailboxRef(), .worldObject = std::move(world) };

		CompletePreparedMainWorld(prepare, std::move(binding), std::move(completed));
	}

	void ClientNetworkManager::CompletePreparedMainWorld(ClientWorldPrepare prepare, ClientWorldBinding binding, std::function<void(bool)> completed)
	{
		AssertPrincipalAffinity();
		const bool succeeded = binding.mailbox.IsValid() && binding.worldObject && m_principal.mainWorld.Prepare(prepare, binding.mailbox, binding.worldObject);

		if (!succeeded && binding.worldObject && binding.mailbox.IsValid())
			binding.worldObject->Shutdown(eMailboxCloseMode::Abort);

		if (completed)
			completed(succeeded);
	}

	bool ClientNetworkManager::CommitMainWorld(const ClientWorldCommit& commit)
	{
		AssertPrincipalAffinity();
		auto& mainWorld = m_principal.mainWorld;
		const auto previous = mainWorld.Current();

		if (!mainWorld.Commit(commit))
			return false;

		const auto& current = mainWorld.Current();
		if (current && current->worldObject)
		{
			const WorldUserContext worldUser
			{
				.userId = GetUserId(),
				.mainRevision = commit.correlation.mainRevision,
			};
			if (!current->worldObject->AddMember(worldUser))
				current->worldObject->UpdateMemberContext(worldUser);
			current->worldObject->Start(SIMULATION_TICK_NS);
			const bool presentationReady = mainWorld.MarkPresentationReady(current->world);
			JAM_ASSERT(presentationReady);
		}
		else
		{
			return false;
		}

		if (previous && current && previous->mailbox.ownerId != current->mailbox.ownerId && previous->worldObject)
		{
			previous->worldObject->RemoveMember(GetUserId());
			previous->worldObject->Shutdown(eMailboxCloseMode::Abort);
		}

		return true;
	}

	bool ClientNetworkManager::ApplyMainWorldChanged(const UserWorldState& state)
	{
		AssertPrincipalAffinity();

		auto& mainWorld = m_principal.mainWorld;
		const auto current  = mainWorld.Current();
		const auto prepared = mainWorld.Prepared();

		if (!mainWorld.ApplyAuthoritative(state))
			return false;
		if (state.main)
			return true;

		const UserId userId = GetUserId();
		auto destroy = [userId](const std::optional<ClientWorldBinding>& binding)
			{
				if (!binding || !binding->worldObject) return;
				binding->worldObject->RemoveMember(userId);
				binding->worldObject->Shutdown(eMailboxCloseMode::Abort);
			};

		destroy(current);
		if (!current || !prepared || current->mailbox.ownerId != prepared->mailbox.ownerId)
			destroy(prepared);

		return true;
	}

	bool ClientNetworkManager::DispatchWorldPacket(UserId userId, WorldId worldId, Packet packet)
	{
		AssertPrincipalAffinity();
		if (!packet.IsValid())
			return false;

		const auto& current = m_principal.mainWorld.Current();
		if (current && current->packetReady && current->worldObject && current->world.worldId == worldId)
		{
			current->worldObject->HandleWorldPacket(userId, std::move(packet));
			return true;
		}

		const auto& prepared = m_principal.mainWorld.Prepared();
		if (prepared && prepared->packetReady && prepared->worldObject && prepared->world.worldId == worldId)
		{
			prepared->worldObject->HandleWorldPacket(userId, std::move(packet));
			return true;
		}

		return false;
	}





	void ClientNetworkManager::PublishNetworkStateEvent() const
	{
		NetworkStateEvent evt{};
		evt.clientInstanceId = m_clientInstanceId;
		evt.accountId = GetAccountId();
		evt.userId	  = GetUserId();
		evt.state.phase = m_running.load(std::memory_order_acquire)
			? (m_sessionReady.load(std::memory_order_acquire) ? eNetworkPhase::Ready : eNetworkPhase::Connecting)
			: eNetworkPhase::Disconnected;
		evt.state.bootstrapKind = m_bootstrapKind.load(std::memory_order_acquire);
		GLOBAL_EVENTBUS_PUBLISH(evt);
	}

	void ClientNetworkManager::UpdateSessionReadyState()
	{
		const bool tcpBound = m_tcpBound.load(std::memory_order_acquire);
		const bool udpBound = m_udpBound.load(std::memory_order_acquire);
		const eBootstrapKind bootstrapKind = m_bootstrapKind.load(std::memory_order_acquire);
		const bool ready = tcpBound && udpBound && bootstrapKind != eBootstrapKind::Pending;
		const bool previous = m_sessionReady.exchange(ready, std::memory_order_acq_rel);
		JAMNET_LOG_INFO("[NetworkReady] evaluated. userId={}, tcpBound={}, udpBound={}, bootstrapKind={}, ready={}, previous={}",
			m_principal.userId, tcpBound, udpBound, static_cast<uint32>(bootstrapKind), ready, previous);

		if (m_principal.tcp) m_principal.tcp->SetReady(ready);
		if (m_principal.udp) m_principal.udp->SetReady(ready);

		if (previous == ready)
			return;

		PublishNetworkStateEvent();
	}

	bool ClientNetworkManager::IsOnPrincipalShard() const
	{
		const auto* local = CurrentShardLocal();
		return local && m_principalShard && local->shardIndex == static_cast<uint32>(m_principalShard->GetIndex());
	}

	void ClientNetworkManager::AssertPrincipalAffinity() const
	{
		JAM_ASSERT(IsOnPrincipalShard());
	}

}
