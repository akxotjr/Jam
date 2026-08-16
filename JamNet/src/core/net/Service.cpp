#include "pch.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/RPC.h"
#include "jamnet/core/net/Service.h"

#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace jam::net
{
	namespace
	{
		inline constexpr uint64 kClientUdpCloseTimeout = 12_s;
	}


	namespace
	{
		void WaitPendingIocpDrain(const char* name, IocpObject* object, const uint64 warningThreshold = 250_ms)
		{
			if (!object)
				return;

			const auto warningDeadline = NOW_NS() + warningThreshold;
			while (object->GetPendingDispatchCount() > 0 && NOW_NS() < warningDeadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));

			if (object->GetPendingDispatchCount() > 0)
				JAM_LOG_WARN("[Service] IOCP drain delayed for {}. pending={}; waiting for completion", name, object->GetPendingDispatchCount());

			while (object->GetPendingDispatchCount() > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}



	Service::Service(const ServiceConfig& config) : m_config(config)
	{
	}

	Service::~Service()
	{
		Service::CloseService();
	}

	void Service::Init()
	{
	}

	void Service::CloseService()
	{
		if (m_transportClosed.exchange(true, std::memory_order_acq_rel))
			return;

		m_running.store(false, std::memory_order_relaxed);

		if (m_listener)  m_listener->CloseSocket();
		if (m_udpRouter) m_udpRouter->CloseSocket();
		WaitPendingIocpDrain("TcpListener", m_listener.get());
		WaitPendingIocpDrain("UdpRouter", m_udpRouter.get());
		m_listener.reset();
		m_udpRouter.reset();
		m_iocpCore.reset();

		GlobalExecutor::Instance().ConveyAll(Job([] { FlushNetBufferThreadLocalCaches(); }));
		FlushNetBufferThreadLocalCaches();
	}


	std::unique_ptr<TcpSession> Service::CreateTcpSession()
	{
		return CreateTcpSession(GetRemoteTcpNetAddress());
	}

	std::unique_ptr<UdpSession> Service::CreateUdpSession()
	{
		return CreateUdpSession(GetRemoteUdpNetAddress());
	}

	std::unique_ptr<TcpSession> Service::CreateTcpSession(const NetAddress& remoteAddr)
	{
		return MakeTcpSession(remoteAddr);
	}

	std::unique_ptr<UdpSession> Service::CreateUdpSession(const NetAddress& remoteAddr)
	{
		return MakeUdpSession(remoteAddr);
	}

	std::unique_ptr<TcpSession> Service::MakeTcpSession(const NetAddress& remoteAddr)
	{
		if (!m_tcpFactory || !remoteAddr.IsValid())
			return nullptr;

		std::unique_ptr<TcpSession> session = m_tcpFactory();
		if (!session) return nullptr;

		session->SetService(this);
		if (m_sessionInitCallback) m_sessionInitCallback(session.get());
		session->Init(remoteAddr);

		return session;
	}

	std::unique_ptr<UdpSession> Service::MakeUdpSession(const NetAddress& remoteAddr)
	{
		if (!m_udpFactory || !remoteAddr.IsValid())
			return nullptr;

		std::unique_ptr<UdpSession> session = m_udpFactory();
		if (!session) return nullptr;

		session->SetService(this);
		if (m_sessionInitCallback) m_sessionInitCallback(session.get());
		session->Init(remoteAddr);

		return session;
	}

	void Service::NotifyTcpSessionAttached()
	{
		m_tcpSessionCount.fetch_add(1, std::memory_order_relaxed);
		m_sessionCount.fetch_add(1, std::memory_order_relaxed);
	}

	void Service::ReleaseTcpSession(TcpSession* session)
	{
		if (!session)
			return;

		TcpSession* const sessionPtr = session;
		const SessionId sessionId = session->GetSessionId();
		const EndpointHandle endpoint = session->GetEndpointHandle();
		auto self = shared_from_this();
		session->Post(Job([self = std::move(self), sessionPtr, sessionId, endpoint]
			{
				auto& L = CurrentShardLocalChecked();
				std::unique_ptr<Session> detached = {};

				if (sessionId == kInvalidSessionId)
				{
					auto& table = GetPreboundSessionTable(L);
					auto it = table.find(endpoint);
					if (it == table.end() || it->second.get() != sessionPtr)
						return;
					detached = std::move(it->second);
					table.erase(it);
				}
				else
				{
					detached = L.sessionState->ReleaseBoundSession(sessionId, sessionPtr);
					if (!detached)
						return;
					L.sessionState->FreeSessionId(sessionId);
				}

				auto closing = std::shared_ptr<Session>(std::move(detached));
				Mailbox* mailbox = closing->m_mailboxRef.mailbox;
				auto finalize = [self, closing = std::move(closing)]() mutable
					{
						self->DestroySessionEntity(*closing);
						closing->m_mailboxRef = {};
						closing->FinalizeShardOwnedClose();
						self->m_tcpSessionCount.fetch_sub(1, std::memory_order_relaxed);
						self->m_sessionCount.fetch_sub(1, std::memory_order_relaxed);
					};

				if (mailbox)
					mailbox->Close(eMailboxCloseMode::Drain, std::move(finalize));
				else
					finalize();
			}, eJobPriority::Control));
	}

	void Service::NotifyUdpSessionAttached(uint64 endpointId, RouteKey routeKey, uint32 generation)
	{
		if (m_udpRouter)
			m_udpRouter->RegisterIngressPrebindRoute(endpointId, routeKey, generation);

		m_udpSessionCount.fetch_add(1, std::memory_order_relaxed);
		m_sessionCount.fetch_add(1, std::memory_order_relaxed);
	}

	void Service::ReleaseUdpSession(UdpSession* session)
	{
		if (!session) return;

		const uint64 endpointId = session->GetEndpointId();
		UdpSession* const sessionPtr = session;
		const SessionId sessionId = session->GetSessionId();
		const EndpointHandle endpoint = session->GetEndpointHandle();
		auto self = shared_from_this();
		session->Post(Job([self = std::move(self), endpointId, sessionPtr, sessionId, endpoint]
			{
				auto& L = CurrentShardLocalChecked();
				std::unique_ptr<Session> detached = {};

				if (sessionId == kInvalidSessionId)
				{
					auto& table = GetPreboundSessionTable(L);
					auto it = table.find(endpoint);
					if (it == table.end() || it->second.get() != sessionPtr)
						return;
					detached = std::move(it->second);
					table.erase(it);
				}
				else
				{
					detached = L.sessionState->ReleaseBoundSession(sessionId, sessionPtr);
					if (!detached)
						return;
					L.sessionState->FreeSessionId(sessionId);
				}

				auto closing = std::shared_ptr<Session>(std::move(detached));
				Mailbox* mailbox = closing->m_mailboxRef.mailbox;
				auto finalize = [self, endpointId, closing = std::move(closing)]() mutable
					{
						self->DestroySessionEntity(*closing);
						closing->m_mailboxRef = {};
						closing->FinalizeShardOwnedClose();
						if (self->m_udpRouter)
							self->m_udpRouter->ClearIngressRoute(endpointId);

						self->m_udpSessionCount.fetch_sub(1, std::memory_order_relaxed);
						self->m_sessionCount.fetch_sub(1, std::memory_order_relaxed);
					};

				if (mailbox)
					mailbox->Close(eMailboxCloseMode::Drain, std::move(finalize));
				else
					finalize();
			}, eJobPriority::Control));
	}

	Session* Service::FindOwnedSession(SessionId sessionId, const EndpointHandle& endpoint, uint32 generation)
	{
		(void)generation;
		return GetOrCreateSessionShardState(CurrentShardLocalChecked()).FindSessionAny(sessionId, endpoint);
	}

	bool Service::RegisterIocpObject(IocpObject* object)
	{
		IocpCore* core = GetIocpCore();
		return core && core->Register(object);
	}



	ClientService::ClientService(const ServiceConfig& config, uint64 accountId, std::shared_ptr<ShardExecutor> principalShard)
		: Service(config), m_accountId(accountId), m_principalShard(std::move(principalShard))
	{
		m_type = eServiceType::CLIENT;
	}

	bool ClientService::Start()
	{
		if (!m_principalShard)
			return false;
		if (!HasUdpFactory())
			return false;

		m_iocpCore = GlobalExecutor::Instance().AcquireIocpCore();
		if (!m_iocpCore)
			return false;
		m_running.store(true, std::memory_order_relaxed);

		m_udpRouter = std::make_shared<UdpRouter>();
		if (m_udpRouter->Start(this) == false)
		{
			CloseService();
			return false;
		}

		return true;
	}

	void ClientService::CloseService()
	{
		JAM_ASSERT(IsOnPrincipalShard());
		BeginClose();
	}

	void ClientService::BeginClose(std::function<void()> completed)
	{
		JAM_ASSERT(IsOnPrincipalShard());
		if (completed)
			m_closeCompleted.push_back(std::move(completed));

		if (m_closePhase == eClosePhase::Closed)
		{
			auto callbacks = std::move(m_closeCompleted);
			m_closeCompleted.clear();
			for (auto& callback : callbacks)
				if (callback) callback();
			return;
		}
		if (m_closePhase != eClosePhase::Running)
			return;

		m_running.store(false, std::memory_order_release);
		BeginUdpClose();
	}

	void ClientService::BeginUdpClose()
	{
		JAM_ASSERT(IsOnPrincipalShard());
		m_closePhase = eClosePhase::ClosingUdp;
		const uint64 token = ++m_closeToken;

		if (!m_udpSession)
		{
			BeginTcpClose();
			return;
		}

		m_udpSession->Disconnect();
		const std::weak_ptr<ClientService> weak = std::static_pointer_cast<ClientService>(shared_from_this());
		m_principalShard->SubmitAfter(Job([weak, token]()
			{
				if (const auto self = weak.lock())
					self->OnUdpCloseTimeout(token);
			}, eJobPriority::Control), kClientUdpCloseTimeout);
	}

	void ClientService::BeginTcpClose()
	{
		JAM_ASSERT(IsOnPrincipalShard());
		m_closePhase = eClosePhase::ClosingTcp;
		const uint64 token = ++m_closeToken;

		if (!m_tcpSession)
		{
			FinalizeClose();
			return;
		}

		m_tcpSession->Disconnect();
		const std::weak_ptr<ClientService> weak = std::static_pointer_cast<ClientService>(shared_from_this());
		m_principalShard->SubmitAfter(Job([weak, token]()
			{
				if (const auto self = weak.lock())
					self->OnTcpCloseTimeout(token);
			}, eJobPriority::Control), 1_s);
	}

	void ClientService::OnUdpCloseTimeout(uint64 token)
	{
		JAM_ASSERT(IsOnPrincipalShard());
		if (m_closePhase != eClosePhase::ClosingUdp || m_closeToken != token)
			return;

		JAM_LOG_WARN("[ClientService] UDP graceful close timed out; forcing session close");
		WaitPendingIocpDrain("ClientUdpSession", m_udpSession.get());
		DestroyUdpSession(m_udpSession.get());
		BeginTcpClose();
	}

	void ClientService::OnTcpCloseTimeout(uint64 token)
	{
		JAM_ASSERT(IsOnPrincipalShard());
		if (m_closePhase != eClosePhase::ClosingTcp || m_closeToken != token)
			return;

		JAM_LOG_WARN("[ClientService] TCP close timed out; forcing socket close");
		if (m_tcpSession)
			m_tcpSession->SetSocket(INVALID_SOCKET);
		WaitPendingIocpDrain("ClientTcpSession", m_tcpSession.get());
		DestroyTcpSession(m_tcpSession.get());
		FinalizeClose();
	}

	void ClientService::FinalizeClose()
	{
		JAM_ASSERT(IsOnPrincipalShard());
		if (m_closePhase == eClosePhase::ClosingTransport || m_closePhase == eClosePhase::Closed)
			return;

		m_closePhase = eClosePhase::ClosingTransport;
		Service::CloseService();
		m_closePhase = eClosePhase::Closed;
		++m_closeToken;

		auto callbacks = std::move(m_closeCompleted);
		m_closeCompleted.clear();
		for (auto& callback : callbacks)
			if (callback) callback();
	}

	bool ClientService::AttachTcpSession(std::unique_ptr<TcpSession> session)
	{
		JAM_ASSERT(IsOnPrincipalShard());
		if (!session || m_tcpSession)
			return false;

		session->m_shard = m_principalShard;
		session->m_serviceGeneration = m_tcpGeneration;
		m_tcpSession = std::move(session);
		NotifyTcpSessionAttached();
		return true;
	}

	bool ClientService::AttachUdpSession(std::unique_ptr<UdpSession> session)
	{
		JAM_ASSERT(IsOnPrincipalShard());
		if (!session || m_udpSession)
			return false;

		auto* raw = session.get();
		const uint64 endpointId = raw->GetEndpointId();
		const RouteKey routeKey = raw->GetRouteKey();
		session->m_shard = m_principalShard;
		session->m_serviceGeneration = m_udpGeneration;
		m_udpSession = std::move(session);
		NotifyUdpSessionAttached(endpointId, routeKey, m_udpGeneration);
		return true;
	}

	TcpSession* ClientService::FindTcpSession() const
	{
		JAM_ASSERT(IsOnPrincipalShard());
		return m_tcpSession.get();
	}

	UdpSession* ClientService::FindUdpSession() const
	{
		JAM_ASSERT(IsOnPrincipalShard());
		return m_udpSession.get();
	}

	void ClientService::ReleaseTcpSession(TcpSession* session)
	{
		JAM_ASSERT(IsOnPrincipalShard());
		if (!session || m_tcpSession.get() != session)
			return;

		DestroyTcpSession(session);
		if (m_closePhase == eClosePhase::ClosingTcp)
			FinalizeClose();
	}

	void ClientService::ReleaseUdpSession(UdpSession* session)
	{
		JAM_ASSERT(IsOnPrincipalShard());
		if (!session || m_udpSession.get() != session)
			return;

		DestroyUdpSession(session);
		if (m_closePhase == eClosePhase::ClosingUdp)
			BeginTcpClose();
	}

	bool ClientService::IsOnPrincipalShard() const
	{
		const auto* local = CurrentShardLocal();
		return local && m_principalShard && local->shardIndex == static_cast<uint32>(m_principalShard->GetIndex());
	}

	void ClientService::DestroyTcpSession(const TcpSession* expected)
	{
		if (!expected || m_tcpSession.get() != expected)
			return;

		auto owner = std::move(m_tcpSession);
		DestroySessionEntity(*owner);

		if (owner->m_mailboxRef.mailbox)
			m_principalShard->CloseMailbox(owner->m_mailboxRef.mailbox->GetId(), eMailboxCloseMode::Abort);

		owner->m_mailboxRef = {};
		owner->FinalizeShardOwnedClose();

		++m_tcpGeneration;
		m_tcpSessionCount.fetch_sub(1, std::memory_order_relaxed);
		m_sessionCount.fetch_sub(1, std::memory_order_relaxed);
	}

	void ClientService::DestroyUdpSession(const UdpSession* expected)
	{
		if (!expected || m_udpSession.get() != expected)
			return;

		auto owner = std::move(m_udpSession);
		if (m_udpRouter)
			m_udpRouter->ClearIngressRoute(owner->GetEndpointId());

		DestroySessionEntity(*owner);
		if (owner->m_mailboxRef.mailbox)
			m_principalShard->CloseMailbox(owner->m_mailboxRef.mailbox->GetId(), eMailboxCloseMode::Abort);

		owner->m_mailboxRef = {};
		owner->FinalizeShardOwnedClose();

		++m_udpGeneration;
		m_udpSessionCount.fetch_sub(1, std::memory_order_relaxed);
		m_sessionCount.fetch_sub(1, std::memory_order_relaxed);
	}

	void Service::DestroySessionEntity(Session& session)
	{
		auto& R = CurrentShardLocalChecked().registry;
		const entt::entity entity = session.m_entity;
		if (entity == entt::null || !R.valid(entity))
		{
			session.m_entity = entt::null;
			return;
		}

		if (auto* rpc = R.try_get<RpcState>(entity))
		{
			auto inflight = std::move(rpc->inflight);
			rpc->inflight.clear();
			for (auto& await : inflight | std::views::values)
			{
				if (await.onDone)
					await.onDone(false);
			}
		}

		RPCUnregisterAll(R, entity);
		R.destroy(entity);
		session.m_entity = entt::null;
	}

	Session* ClientService::FindOwnedSession(SessionId sessionId, const EndpointHandle& endpoint, uint32 generation)
	{
		JAM_ASSERT(IsOnPrincipalShard());
		auto matches = [sessionId, &endpoint](const Session* session)
			{
				if (!session)
					return false;
				if (sessionId != kInvalidSessionId)
					return session->GetSessionId() == sessionId && session->MatchesEndpointHandle(endpoint);
				return session->MatchesEndpointHandle(endpoint);
			};

		if ((generation == 0 || generation == m_tcpGeneration) && matches(m_tcpSession.get()))
			return m_tcpSession.get();
		if ((generation == 0 || generation == m_udpGeneration) && matches(m_udpSession.get()))
			return m_udpSession.get();
		return nullptr;
	}



	ServerService::ServerService(const ServiceConfig& config) : Service(config)
	{
		m_type = eServiceType::SERVER;
	}

	bool ServerService::Start()
	{
		if (!CanStart())
			return false;

		m_iocpCore = GlobalExecutor::Instance().AcquireIocpCore();
		if (!m_iocpCore)
			return false;
		m_running.store(true, std::memory_order_relaxed);

		bool startedAny = false;

		if (HasTcpFactory() && GetLocalTcpNetAddress().IsValid())
		{
			m_listener = std::make_shared<TcpListener>();
			if (m_listener->StartAccept(this) == false)
			{
				CloseService();
				return false;
			}
			startedAny = true;
		}

		if (HasUdpFactory() && GetLocalUdpNetAddress().IsValid())
		{
			m_udpRouter = std::make_shared<UdpRouter>();
			if (m_udpRouter->Start(this) == false)
			{
				CloseService();
				return false;
			}
			startedAny = true;
		}

		if (!startedAny)
		{
			CloseService();
			return false;
		}

		return true;
	}

	void ServerService::CloseService()
	{
		if (m_sessionsClosed.exchange(true, std::memory_order_acq_rel))
		{
			Service::CloseService();
			return;
		}

		m_running.store(false, std::memory_order_release);

		struct CloseBarrier
		{
			std::mutex				mutex;
			std::condition_variable completed;
			size_t					remaining = 0;
		};

		auto shards  = GlobalExecutor::Instance().GetShards();
		auto barrier = std::make_shared<CloseBarrier>();
		for (const auto& shard : shards)
		{
			if (shard)
				++barrier->remaining;
		}

		auto self = std::static_pointer_cast<ServerService>(shared_from_this());
		auto closeShardSessions = [self, barrier]()
			{
				auto& L = CurrentShardLocalChecked();
				std::vector<std::unique_ptr<Session>> closing;
				if (L.sessionState)
				{
					auto& prebound = GetPreboundSessionTable(L);
					for (auto it = prebound.begin(); it != prebound.end();)
					{
						Session* session = it->second.get();
						if (session && session->GetService() == self.get())
						{
							closing.push_back(std::move(it->second));
							it = prebound.erase(it);
							continue;
						}
						++it;
					}

					for (auto& entry : L.sessionState->sessionsById.entries)
					{
						if (!entry.object || entry.object->GetService() != self.get())
							continue;

						closing.push_back(std::move(entry.object));
						entry.state = eShardOwnedObjectState::Destroyed;
						L.sessionState->sessionsById.freeIndices.push_back(entry.index);
					}
				}

				for (auto& session : closing)
				{
					session->SetService(nullptr);
					session->Disconnect();
					WaitPendingIocpDrain("ServerSession", session.get());
					session->FinalizeShardOwnedClose();
				}

				std::scoped_lock lock(barrier->mutex);
				if (--barrier->remaining == 0)
					barrier->completed.notify_one();
			};

		const auto* current = CurrentShardLocal();
		for (const auto& shard : shards)
		{
			if (!shard)
				continue;
			if (current && current->shardIndex == static_cast<uint32>(shard->GetIndex()))
				closeShardSessions();
			else
				shard->Submit(Job(closeShardSessions, eJobPriority::Control));
		}

		{
			std::unique_lock lock(barrier->mutex);
			barrier->completed.wait(lock, [&barrier] { return barrier->remaining == 0; });
		}

		Service::CloseService();
	}
}
