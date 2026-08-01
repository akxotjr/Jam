#include "pch.h"
#include "jamnet/runtime/application/ServerNetworkManager.h"

#include "jamnet/core/net/Service.h"

#include "jamnet/runtime/world/simulation/server/ServerWorld.h"
#include "jamnet/runtime/world/simulation/server/IServerWorldContent.h"

#include "jamnet/runtime/session/ServerSession.h"
#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/social/SocialService.h"
#include "jamnet/runtime/world/lifecycle/ServerWorldTransitionCoordinator.h"
#include "jamnet/runtime/world/data/SharedDataManifestLoader.h"
#include "jamnet/core/utils/Clock.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"



namespace jam::net
{
	namespace
	{
		Packet MakeClientWorldPreparePacket(const ClientWorldPrepare& prepare)
		{
			flatbuffers::FlatBufferBuilder fbb(128);
			const auto& world = prepare.correlation.world;
			const auto root = fb::CreatefbClientWorldPrepare(fbb, prepare.token.value,
				static_cast<fb::fbClientServerBarrierKind>(prepare.kind), world.instance.instanceId.value,
				world.instance.archetypeKey.v, world.worldId, prepare.correlation.mainRevision, prepare.contentRevision);
			fbb.Finish(root);
			return PacketBuilder::CreateCustomPacket(CustomPacketId::CLIENT_WORLD_PREPARE, PacketFlags::NONE,
				eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), fbb.GetSize());
		}

		Packet MakeClientWorldCommitPacket(const ClientWorldCommit& commit)
		{
			flatbuffers::FlatBufferBuilder fbb(96);
			const auto& world = commit.correlation.world;
			const auto root = fb::CreatefbClientWorldCommit(fbb, commit.token.value, world.instance.instanceId.value,
				world.worldId, commit.correlation.mainRevision);
			fbb.Finish(root);
			return PacketBuilder::CreateCustomPacket(CustomPacketId::CLIENT_WORLD_COMMIT, PacketFlags::NONE,
				eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), fbb.GetSize());
		}

		Packet MakeMainWorldChangedPacket(const UserPhysicalWorldState& state)
		{
			flatbuffers::FlatBufferBuilder fbb(128);
			flatbuffers::Offset<fb::fbMainPhysicalWorld> main;
			if (state.main)
				main = fb::CreatefbMainPhysicalWorld(fbb, state.main->instance.instanceId.value,
					state.main->instance.archetypeKey.v, state.main->worldId);
			const auto wireState = fb::CreatefbUserMainPhysicalWorldState(fbb, main, state.revision);
			const auto root = fb::CreatefbUserMainPhysicalWorldChanged(fbb, wireState);
			fbb.Finish(root);
			return PacketBuilder::CreateCustomPacket(CustomPacketId::USER_MAIN_WORLD_CHANGED, PacketFlags::NONE,
				eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), fbb.GetSize());
		}

		Packet MakeWorldTransitionResultPacket(const WorldTransitionResult& result)
		{
			flatbuffers::FlatBufferBuilder fbb(160);
			flatbuffers::Offset<fb::fbMainPhysicalWorld> main;
			if (result.state.main)
				main = fb::CreatefbMainPhysicalWorld(fbb, result.state.main->instance.instanceId.value,
					result.state.main->instance.archetypeKey.v, result.state.main->worldId);
			const auto state = fb::CreatefbUserMainPhysicalWorldState(fbb, main, result.state.revision);
			const auto kind = result.kind == eWorldTransitionKind::Enter
				? fb::fbWorldTransitionKind_Enter : fb::fbWorldTransitionKind_Leave;
			const auto root = fb::CreatefbWorldTransitionResult(fbb, result.requestId, kind,
				result.transitionToken.value, static_cast<fb::fbWorldTransitionFailure>(result.failure), state);
			fbb.Finish(root);
			return PacketBuilder::CreateCustomPacket(CustomPacketId::WORLD_TRANSITION_RESULT, PacketFlags::NONE,
				eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), fbb.GetSize());
		}
	}

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

	std::unique_ptr<IServerWorldContent> ServerNetworkManager::CreateWorldContent(const WorldConfig& config) const
	{
		return m_config.worldContentFactory ? m_config.worldContentFactory(config) : nullptr;
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
		m_worldTransitions->SetTransport(
			[this](uint64 userId, const ClientWorldPrepare& prepare)
				{ Send(userId, MakeClientWorldPreparePacket(prepare), eProtocolType::TCP); },
			[this](uint64 userId, const ClientWorldCommit& commit)
				{ Send(userId, MakeClientWorldCommitPacket(commit), eProtocolType::TCP); },
			[this](uint64 userId, const WorldTransitionResult& result)
				{ Send(userId, MakeWorldTransitionResultPacket(result), eProtocolType::TCP); },
			[this](uint64 userId, const UserPhysicalWorldState& state)
				{ Send(userId, MakeMainWorldChangedPacket(state), eProtocolType::TCP); });

		if (!StartServerService())
		{
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

		{
			WRITE_LOCK_IDX(kSessionLockIdx)
			m_sessions.clear();
		}

		m_worldTransitions.reset();
		m_socialService.reset();

		JAMNET_LOG_INFO("ServerNetworkManager stopped");
	}


	void ServerNetworkManager::EnterWorld(UserId userId, const EnterWorldRequest& request)
	{
		JAMNET_LOG_INFO("[EnterWorld] requested. userId={}, requestId={}, archetype={}, destination={}",
			userId, request.requestId, request.archetypeKey.v, request.destinationName);

		if (!m_worldTransitions)
		{
			Send(userId, MakeWorldTransitionResultPacket({ .kind = eWorldTransitionKind::Enter,
				.requestId = request.requestId, .failure = eWorldTransitionFailure::InvalidRequest }), eProtocolType::TCP);
			return;
		}
		m_worldTransitions->Enter(userId, request, NOW_NS());
	}

	void ServerNetworkManager::LeaveWorld(UserId userId, const LeaveWorldRequest& request)
	{
		if (!m_worldTransitions)
		{
			Send(userId, MakeWorldTransitionResultPacket({ .kind = eWorldTransitionKind::Leave,
				.requestId = request.requestId, .failure = eWorldTransitionFailure::InvalidRequest }), eProtocolType::TCP);
			return;
		}
		auto* local = CurrentShardLocal();
		if (!local || local->shardIndex != GetUserShardIndex(userId))
		{
			Send(userId, MakeWorldTransitionResultPacket({ .kind = eWorldTransitionKind::Leave,
				.requestId = request.requestId, .failure = eWorldTransitionFailure::InvalidRequest }), eProtocolType::TCP);
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
			.userId = user->userId,
			.world = user->physicalWorld,
		};
		return m_socialService->Submit(principal, std::move(command));
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

	void ServerNetworkManager::DestroyWorld(const WorldRuntimeRef& runtime)
	{
		if (m_worldTransitions)
			m_worldTransitions->DestroyRuntime(runtime);
	}

	bool ServerNetworkManager::SubmitWorldJob(const WorldRuntimeRef& runtime, std::function<void(WorldBase&)> job)
	{
		return m_worldTransitions ? m_worldTransitions->SubmitWorldJob(runtime, std::move(job)) : false;
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
				if (it->second.udp.TryGetRaw() == udp)
					it->second.udp.Set(nullptr);
			}
		}

	}

	void ServerNetworkManager::ReleaseSession(UserId userId, const ServerTcpSession* tcp)
	{
		if (userId == kInvalidUserId || !tcp)
			return;

		auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
		UserContext* user = userState.FindUserContext(userId);
		if (!user || user->tcp != tcp->GetSessionId())
			return;

		const ServerSessionBundle sessions = GetSessionBundle(userId);
		const SessionRef<ServerUdpSession> udpRef = sessions.udp;
		udpRef.TryPost(Job([udpRef]()
			{
				if (auto* udp = udpRef.TryGet(); udp && !udp->IsClosing())
					udp->Disconnect();
			}, eJobPriority::Control));

		if (m_worldTransitions)
			m_worldTransitions->OnDisconnected(userId);
		if (m_socialService)
			m_socialService->NotifyDisconnected(userId);

		user = userState.FindUserContext(userId);
		if (user)
		{
			if (user->tcp == tcp->GetSessionId())
				user->tcp = kInvalidSessionId;
			user->udp = kInvalidSessionId;

			if (!user->physicalWorld.main && !user->worldTransition.active)
				userState.FreeUserContext(userId);
		}

		{
			WRITE_LOCK_IDX(kSessionLockIdx);
			auto it = m_sessions.find(userId);
			if (it != m_sessions.end() && it->second.tcp.TryGetRaw() == tcp)
				m_sessions.erase(it);
		}
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
			else
				JAMNET_LOG_WARN("[ServerNetworkManager::Send] session is nullptr or not conntected");
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

	void ServerNetworkManager::Multicast(const WorldRuntimeRef& runtime, Packet packet)
	{
		SubmitWorldJob(runtime, [packet = std::move(packet)](WorldBase& world) mutable
			{
				if (auto* host = dynamic_cast<WorldMembershipHost*>(&world))
					host->Multicast(std::move(packet));
			});
	}

	void ServerNetworkManager::Broadcast(Packet packet)
	{
		READ_LOCK_IDX(kSessionLockIdx);
		for (const auto& [_, sessions] : m_sessions)
			if (auto* tcp = sessions.tcp.TryGet(); tcp && tcp->IsConnected())
				tcp->Send(packet);
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
