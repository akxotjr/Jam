#include "pch.h"
#include "SessionEndpoint.h"

namespace jam::net
{
	SessionEndpoint::SessionEndpoint(utils::exec::ShardDirectory& dir, uint64 routeKey, utils::exec::RouteSeed seed)
		: m_dir(&dir), m_key(routeKey), m_routing(seed)
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

	void SessionEndpoint::PostGroup(uint64 group_id, utils::job::Job j)
	{
		if (m_closed.load(std::memory_order_acquire)) return;

		const uint64 rk = m_routing.KeyForGroup(group_id);
		auto ep = m_dir->EndpointFor(rk, utils::exec::eMailboxChannel::NORMAL);
		(void)ep.Post(utils::job::Job([group_id, j = std::move(j)]() mutable {
				auto& shard = /* ShardLocal() or capture via TLS */;
				shard.OnGroupMulticastHome(group_id, std::move(j));
			}));
	}

	void SessionEndpoint::RebindKey(uint64 newKey)
	{
		m_key = newKey;
		RefreshEnpoint();
	}

	void SessionEndpoint::BeginDrain()
	{
		m_closed.store(true, std::memory_order_release);
	}

	void SessionEndpoint::JoinGroup(uint64 group_id)
	{
		if (m_closed.load(std::memory_order_acquire)) return;
		EnsureBound(); // ���� Mailbox ����

		// 1) �� ����(���� routeKey ����)���� ���� ���� ���
		const uint64 shard_id = m_dir->PickShard(m_key);
		auto shard = m_dir->ShardAt(shard_id);
		if (shard) {
			auto q = m_mbNorm; // Normal ä�η� ����
			shard->Submit(utils::job::Job([shard, group_id, q] {
					shard->OnGroupLocalJoin(group_id, q);
				}));
		}

		// 2) Ȩ ���忡 ���� ���忡 ��� 1�� �߰��� ���� (Ctrl ä�� ����)
		const uint64 rk = m_routing.KeyForGroup(group_id);
		auto epCtrl = m_dir->EndpointFor(rk, utils::exec::eMailboxChannel::CTRL);
		epCtrl.Post(utils::job::Job([group_id, myShardIdx = static_cast<uint32>(shard_id)] {
			// Ȩ ���� ���ؽ�Ʈ���� ����
				auto& shard = /* ShardLocal() or capture via TLS */;
				shard.OnGroupHomeMark(group_id, myShardIdx, +1);
			}));
	}

	void SessionEndpoint::LeaveGroup(uint64 group_id)
	{
		if (m_closed.load(std::memory_order_acquire)) return;

		// 1) �� ���忡�� ���� ����
		const uint64 shard_id = m_dir->PickShard(m_key);
		auto shard = m_dir->ShardAt(shard_id);
		if (shard) {
			auto q = m_mbNorm;
			shard->Submit(utils::job::Job([shard, group_id, q] {
					shard->OnGroupLocalLeave(group_id, q);
				}));
		}

		// 2) Ȩ ���� refcnt -1
		const uint64 rk = m_routing.KeyForGroup(group_id);
		auto epCtrl = m_dir->EndpointFor(rk, utils::exec::eMailboxChannel::CTRL);
		epCtrl.Post(utils::job::Job([group_id, myShardIdx = static_cast<uint32>(shard_id)] {
				auto& shard = /* ShardLocal() or capture via TLS */;
				shard.OnGroupHomeMark(group_id, myShardIdx, -1);
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

		const size_t sid = m_dir->PickShard(m_key);
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
		const uint64 sid = m_dir->PickShard(m_key);	//
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

		// 1) lazy ���ε�: ���� ť ����
		EnsureBound();

		// 2) ���� ��������Ʈ�� ���� Post (gen/state üũ)
		utils::exec::ShardEndpoint& ep = (ch == utils::exec::eMailboxChannel::NORMAL) ? m_epNorm : m_epCtrl;
		auto r = ep.Post(std::move(j));
		if (r == utils::exec::ShardEndpoint::ePostResult::OK) return;

		if (r == utils::exec::ShardEndpoint::ePostResult::STALE || r == utils::exec::ShardEndpoint::ePostResult::UNVAILABLE)
		{
			// 3) �ֽ� ��������Ʈ ��ȹ��
			ep = m_dir->EndpointFor(m_key, ch);

			// 4) (�߿�) �����ڰ� �ٲ������ ���� Mailbox�� �� ���忡 ����ε�
			RebindIfExecutorChanged();

			// 5) 1ȸ ��õ�
			(void)ep.Post(std::move(j));
			return;
		}

		// Draining/Closed �� ��å������ drop/buffer
		// �ʿ��ϸ� ���⼭ �α� or ī���� ����
	}


}
