#include "pch.h"
#include "SessionEndpoint.h"

namespace jam::net
{
	SessionEndpoint::SessionEndpoint(utils::exec::ShardDirectory& dir, utils::exec::RouteKey key)
		: m_dir(&dir), m_key(key)
	{
		RefreshEnpoint();
	}

	void SessionEndpoint::Post(utils::job::Job j)
	{
		PostImpl(std::move(j), utils::exec::eMailboxChannel::NORMAL);
	}

	void SessionEndpoint::PostCtrl(utils::job::Job j)
	{
		PostImpl(std::move(j), utils::exec::eMailboxChannel::CTRL);
	}

	void SessionEndpoint::PostGroup(uint64 group_id, utils::exec::GroupHomeKey gk, utils::job::Job j)
	{
		if (m_closed.load(std::memory_order_acquire)) return;

		// Ȩ ���� �ε���/������ �����͸� �̸� ���� ĸó�� ��
		const uint64 homeShard_id = m_dir->PickShard(gk.v);
		auto home_shard = m_dir->ShardAt(homeShard_id);
		if (!home_shard) return;

		// ������ ��� ��������Ʈ(������ ���� ����)�� ������ TLS ��� this ȣ�� ����
		utils::exec::ShardEndpoint home_ep(home_shard);
		(void)home_ep.Post(utils::job::Job([s = home_shard, group_id, jj = std::move(j)]() mutable {
				s->OnGroupMulticastHome(group_id, std::move(jj));
			}));
	}

	void SessionEndpoint::RebindKey(utils::exec::RouteKey newKey)
	{
		m_key = newKey;
		RefreshEnpoint();
	}

	void SessionEndpoint::BeginDrain()
	{
		m_closed.store(true, std::memory_order_release);
	}

	void SessionEndpoint::JoinGroup(uint64 group_id, utils::exec::GroupHomeKey gk)
	{
		if (m_closed.load(std::memory_order_acquire)) return;
		EnsureBound(); // ���� Mailbox ����

		// 1) �� ����(���� routeKey ����)���� ���� ���� ���
		const uint64 myShard_id = m_dir->PickShard(m_key.value());
		auto my_shard = m_dir->ShardAt(myShard_id);
		if (my_shard) 
		{
			auto q = m_mbNorm; // Normal ä�� ����
			my_shard->Submit(utils::job::Job([s = my_shard, group_id, q] {
					s->OnGroupLocalJoin(group_id, q);
				}));
		}

		// 2) Ȩ ���忡 ���� ���忡 ��� +1�� ���� (Ctrl ä�� ����)
		const uint64 homeShard_id = m_dir->PickShard(gk.v);
		auto home_shard = m_dir->ShardAt(homeShard_id);
		if (!home_shard) return;

		utils::exec::ShardEndpoint epCtrl(home_shard);
		epCtrl.Post(utils::job::Job([s = home_shard, group_id, myIdx = static_cast<uint32>(myShard_id)] {
				s->OnGroupHomeMark(group_id, myIdx, +1);
			}));
	}

	void SessionEndpoint::LeaveGroup(uint64 group_id, utils::exec::GroupHomeKey gk)
	{
		if (m_closed.load(std::memory_order_acquire)) return;

		// 1) �� ���忡�� ���� ����
		const uint64 myShard_id = m_dir->PickShard(m_key.value());
		auto my_shard = m_dir->ShardAt(myShard_id);
		if (my_shard) 
		{
			auto q = m_mbNorm;
			my_shard->Submit(utils::job::Job([s = my_shard, group_id, q] {
					s->OnGroupLocalLeave(group_id, q);
				}));
		}

		// 2) Ȩ ���� refcnt -1
		const uint64 homeShard_id = m_dir->PickShard(gk.v);
		auto home_shard = m_dir->ShardAt(homeShard_id);
		if (!home_shard) return;

		utils::exec::ShardEndpoint epCtrl(home_shard);
		epCtrl.Post(utils::job::Job([s = home_shard, group_id, myIdx = static_cast<uint32>(myShard_id)] {
				s->OnGroupHomeMark(group_id, myIdx, -1);
			}));
	}

	void SessionEndpoint::RefreshEnpoint()
	{
		m_epNorm = m_dir->EndpointFor(m_key, utils::exec::eMailboxChannel::NORMAL);
		m_epCtrl = m_dir->EndpointFor(m_key, utils::exec::eMailboxChannel::CTRL);
	}

	void SessionEndpoint::EnsureBound()
	{
		if (m_mbNorm && m_mbCtrl && !m_boundShard.expired())
			return;

		WRITE_LOCK
		if (m_mbNorm && m_mbCtrl && !m_boundShard.expired())
			return;

		const size_t sid = m_dir->PickShard(m_key.value());
		auto shard = m_dir->ShardAt(sid);
		if (!shard) return;

		// ���ǿ� Mailbox 2ä�� ����
		auto qN = shard->CreateMailbox(utils::exec::eMailboxChannel::NORMAL);
		auto qC = shard->CreateMailbox(utils::exec::eMailboxChannel::CTRL);

		m_mbNorm = std::move(qN);
		m_mbCtrl = std::move(qC);
		m_boundShard = shard;
	}

	void SessionEndpoint::RebindIfExecutorChanged()
	{
		// Ű �������� ���� ���� ����
		const uint64 sid = m_dir->PickShard(m_key.value());	//
		auto shard = m_dir->ShardAt(sid);
		if (!shard) return;

		auto locked = m_boundShard.lock();
		if (locked == shard) return; // ���� �����ڸ� ���Ϲڽ� ����� ���ʿ�

		WRITE_LOCK
		locked = m_boundShard.lock();
		if (locked == shard) return;

		// �� �����ڿ� �� ���� Mailbox ����
		auto mbN = shard->CreateMailbox(utils::exec::eMailboxChannel::NORMAL);
		auto mbC = shard->CreateMailbox(utils::exec::eMailboxChannel::CTRL);

		// ��ü (�� ť�� �� ���尡 �巹�� �� ����)
		m_mbNorm.swap(mbN);
		m_mbCtrl.swap(mbC);
		m_boundShard = shard;
	}

	void SessionEndpoint::PostImpl(utils::job::Job j, utils::exec::eMailboxChannel ch)
	{
		if (m_closed.load(std::memory_order_acquire)) return;

		// 1) ���� ���� Mailbox�� �õ� (���� ���)
		EnsureBound();
		auto& q = (ch == utils::exec::eMailboxChannel::NORMAL) ? m_mbNorm : m_mbCtrl;
		if (q && q->Post(std::move(j)))
			return;

		// 2) ���� ��: �ֽ� ��������Ʈ ��ȹ�� + ������ ����ε� �� "�� ����" Post
		auto& ep = (ch == utils::exec::eMailboxChannel::NORMAL) ? m_epNorm : m_epCtrl;
		ep = m_dir->EndpointFor(m_key, ch);
		RebindIfExecutorChanged();
		(void)ep.Post(std::move(j));
	}


}
