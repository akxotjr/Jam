#include "pch.h"
#include "ShardEndpoint.h"
#include "ShardExecutor.h"
#include "Mailbox.h"

namespace jam::utils::exec
{
	ShardEndpoint::ShardEndpoint(ShardSlot* slot, eMailboxChannel channel)
		: m_slot(slot), m_channel(channel)
	{
		if (m_slot)
			m_gen = m_slot->ch[E2U(m_channel)].gen.load(std::memory_order_acquire);
	}


	bool ShardEndpoint::Post(const Sptr<Mailbox>& mailbox, job::Job job)
	{
		if (!m_target || !mailbox)
			return false;

		return mailbox->Post(std::move(job));
	}


	ShardEndpoint::ePostResult ShardEndpoint::Post(job::Job job) const
	{
		if (!m_slot) 
			return ePostResult::UNVAILABLE;

		auto& qs = m_slot->ch[E2U(m_channel)];

		// 세대/상태 체크
		uint32 g = qs.gen.load(std::memory_order_acquire);
		if (g != m_gen) 
			return ePostResult::STALE;

		eShardState st = U2E(eShardState, qs.state.load(std::memory_order_acquire));
		if (st == eShardState::CLOSED)   return ePostResult::CLOSED;
		if (st == eShardState::DRAINING) return ePostResult::DRAINING;

		Mailbox* q = qs.q.load(std::memory_order_acquire);
		if (!q) 
			return ePostResult::UNVAILABLE;

		// ConcurrentQueue는 생산자 토큰 최적화를 지원 → Executor 쪽의 TLS 토큰 헬퍼를 쓰고 싶다면
		// 여기서 직접 q->enqueue(...) 대신 Mailbox::Post(...) 경유를 권장
		// (현재 Mailbox::Post는 토큰 없는 경로로 넣고, TLS 최적화는 필요 시 추가)
		return q->Post(std::move(job)) ? ePostResult::OK : ePostResult::UNVAILABLE;
	}
}
