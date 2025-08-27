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

		// ����/���� üũ
		uint32 g = qs.gen.load(std::memory_order_acquire);
		if (g != m_gen) 
			return ePostResult::STALE;

		eShardState st = U2E(eShardState, qs.state.load(std::memory_order_acquire));
		if (st == eShardState::CLOSED)   return ePostResult::CLOSED;
		if (st == eShardState::DRAINING) return ePostResult::DRAINING;

		Mailbox* q = qs.q.load(std::memory_order_acquire);
		if (!q) 
			return ePostResult::UNVAILABLE;

		// ConcurrentQueue�� ������ ��ū ����ȭ�� ���� �� Executor ���� TLS ��ū ���۸� ���� �ʹٸ�
		// ���⼭ ���� q->enqueue(...) ��� Mailbox::Post(...) ������ ����
		// (���� Mailbox::Post�� ��ū ���� ��η� �ְ�, TLS ����ȭ�� �ʿ� �� �߰�)
		return q->Post(std::move(job)) ? ePostResult::OK : ePostResult::UNVAILABLE;
	}
}
