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
		EnsureBound(); // 세션 Mailbox 보장

		// 1) 내 샤드(세션 routeKey 기준)에서 로컬 가입 등록
		const uint64 shard_id = m_dir->PickShard(m_key);
		auto shard = m_dir->ShardAt(shard_id);
		if (shard) {
			auto q = m_mbNorm; // Normal 채널로 수신
			shard->Submit(utils::job::Job([shard, group_id, q] {
					shard->OnGroupLocalJoin(group_id, q);
				}));
		}

		// 2) 홈 샤드에 “내 샤드에 멤버 1명 추가” 통지 (Ctrl 채널 권장)
		const uint64 rk = m_routing.KeyForGroup(group_id);
		auto epCtrl = m_dir->EndpointFor(rk, utils::exec::eMailboxChannel::CTRL);
		epCtrl.Post(utils::job::Job([group_id, myShardIdx = static_cast<uint32>(shard_id)] {
			// 홈 샤드 컨텍스트에서 실행
				auto& shard = /* ShardLocal() or capture via TLS */;
				shard.OnGroupHomeMark(group_id, myShardIdx, +1);
			}));
	}

	void SessionEndpoint::LeaveGroup(uint64 group_id)
	{
		if (m_closed.load(std::memory_order_acquire)) return;

		// 1) 내 샤드에서 로컬 제거
		const uint64 shard_id = m_dir->PickShard(m_key);
		auto shard = m_dir->ShardAt(shard_id);
		if (shard) {
			auto q = m_mbNorm;
			shard->Submit(utils::job::Job([shard, group_id, q] {
					shard->OnGroupLocalLeave(group_id, q);
				}));
		}

		// 2) 홈 샤드 refcnt -1
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

		// 세션용 Mailbox 2채널 생성
		auto qN = shard->CreateMailbox(utils::exec::eMailboxChannel::NORMAL);
		auto qC = shard->CreateMailbox(utils::exec::eMailboxChannel::CTRL);

		m_mbNorm = std::move(qN);
		m_mbCtrl = std::move(qC);
		m_boundShard = shard;
	}

	void SessionEndpoint::RebindIfExecutorChanged()
	{
		// 키 기준으로 현재 샤드 재계산
		const uint64 sid = m_dir->PickShard(m_key);	//
		auto shard = m_dir->ShardAt(sid);
		if (!shard) return;

		auto locked = m_boundShard.lock();
		if (locked == shard) return; // 같은 실행자면 메일박스 재생성 불필요

		WRITE_LOCK
		locked = m_boundShard.lock();
		if (locked == shard) return;

		// 새 실행자에 새 세션 Mailbox 생성
		auto mbN = shard->CreateMailbox(utils::exec::eMailboxChannel::NORMAL);
		auto mbC = shard->CreateMailbox(utils::exec::eMailboxChannel::CTRL);

		// 교체 (구 큐는 구 샤드가 드레인 후 정리)
		m_mbNorm.swap(mbN);
		m_mbCtrl.swap(mbC);
		m_boundShard = shard;
	}

	void SessionEndpoint::PostImpl(utils::job::Job j, utils::exec::eMailboxChannel ch)
	{
		if (m_closed.load(std::memory_order_acquire)) return;

		// 1) lazy 바인딩: 세션 큐 보장
		EnsureBound();

		// 2) 슬롯 엔드포인트로 안전 Post (gen/state 체크)
		utils::exec::ShardEndpoint& ep = (ch == utils::exec::eMailboxChannel::NORMAL) ? m_epNorm : m_epCtrl;
		auto r = ep.Post(std::move(j));
		if (r == utils::exec::ShardEndpoint::ePostResult::OK) return;

		if (r == utils::exec::ShardEndpoint::ePostResult::STALE || r == utils::exec::ShardEndpoint::ePostResult::UNVAILABLE)
		{
			// 3) 최신 엔드포인트 재획득
			ep = m_dir->EndpointFor(m_key, ch);

			// 4) (중요) 실행자가 바뀌었으면 세션 Mailbox를 새 샤드에 재바인딩
			RebindIfExecutorChanged();

			// 5) 1회 재시도
			(void)ep.Post(std::move(j));
			return;
		}

		// Draining/Closed → 정책적으로 drop/buffer
		// 필요하면 여기서 로그 or 카운터 증가
	}


}
