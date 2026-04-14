#include "pch.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/Service.h"

namespace jam::net
{
	namespace
	{
		const RouteDomain kSessionRouteDomain = RouteDomain::From("Session");
	}

	std::atomic<uint64> Session::s_sessionIdGenerator{ 1 };

	Session::Session()
	{
		m_sessionId = s_sessionIdGenerator.fetch_add(1, std::memory_order_relaxed);
	}

	void Session::Init()
	{
		m_key = GLOBAL_EXEC.MakeRouteKey(kSessionRouteDomain, GetSessionId());
		JAMNET_LOG_DEBUG("[Session::Init()] sessionId= {} protocol= {}", GetSessionId(), m_protocol == eProtocolType::UDP ? "udp" : "tcp");
		EnsureBound();
	}

	bool Session::IsServerSide() const
	{
		if (m_service)
			return m_service->GetServiceType() == eServiceType::SERVER;
		return false;
	}

	bool Session::IsClientSide() const
	{
		if (m_service)
			return m_service->GetServiceType() == eServiceType::CLIENT;
		return false;
	}

	void Session::SetService(Service* service)
	{
		m_service = service;
	}


	void Session::Post(Job j) const
	{
		if (m_closed.load(std::memory_order_acquire))
			return;

		const_cast<Session*>(this)->EnsureBound();

		if (m_mailbox)
			m_mailbox->Post(std::move(j));
	}

	void Session::Submit(Job j) const
	{
		if (auto shard = m_boundShard.lock())
			shard->Submit(std::move(j));
	}

	void Session::SubmitAfter(Job j, uint64 delay_ns) const
	{
		if (auto shard = m_boundShard.lock())
			shard->SubmitAfter(std::move(j), delay_ns);
	}






	void Session::EnsureBound()
	{
		if (!m_boundShard.expired() && m_entityReady.load(std::memory_order_acquire))
			return;

		auto shard = GLOBAL_EXEC.GetShard(m_key);
		if (!shard) return;
		if (m_boundShard.expired()) m_boundShard = shard;
		if (!m_mailbox) m_mailbox = shard->CreateMailbox();

		if (m_entityReady.load(std::memory_order_acquire)) return;

		bool expected = false;
		if (!m_entityCreating.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			return;
		
		shard->Submit(Job(Self(), &Session::CreateEntity, eJobPriority::Critical));
	}

	void Session::CreateEntity()
	{
		if (m_entityReady.load(std::memory_order_acquire))
		{
			m_entityCreating.store(false, std::memory_order_release);
			return;
		}

		auto& L = CurrentShardLocalChecked();
		auto& R = L.registry;

		if (m_entity == entt::null)
			m_entity = R.create();

		const entt::entity e = m_entity;

		BootstrapSessionEntity(L, e, this);

		m_entityReady.store(true, std::memory_order_release);
		m_entityCreating.store(false, std::memory_order_release);
	}
}


