#include "pch.h"
#include "SessionEndpoint.h"

// ECS 컴포넌트들 포함
#include "EcsCommon.hpp"
#include "EcsReliability.hpp"
#include "EcsFragment.hpp"
#include "EcsChannel.hpp"
#include "EcsHandshake.hpp"
#include "EcsNetstat.hpp"
#include "EcsCongestionControl.hpp"

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

		// 홈 샤드 인덱스/실행자 포인터를 미리 구해 캡처해 둠
		const uint64 homeShard_id = m_dir->PickShard(gk.v);
		auto home_shard = m_dir->ShardAt(homeShard_id);
		if (!home_shard) return;

		// 실행자 기반 엔드포인트(실행자 직접 지정)로 보내면 TLS 없어도 this 호출 가능
		utils::exec::ShardEndpoint home_ep(home_shard);
		(void)home_ep.Post(utils::job::Job([s = home_shard, group_id, jj = std::move(j)]() mutable {
				s->OnGroupMulticastHome(group_id, std::move(jj));
			}));
	}

	void SessionEndpoint::BindSession(std::weak_ptr<Session> s)
	{
		m_session = std::move(s);
		EnsureBound();
	}

	void SessionEndpoint::EmitConnect()
	{
		Emit(ecs::EvHsConnect{});
	}

	void SessionEndpoint::EmitDisconnect()
	{
		Emit(ecs::EvHsDisconnect{});
	}

	void SessionEndpoint::EmitSend()
	{
		Emit(ecs::EvReSendR{});
	}

	void SessionEndpoint::EmitRecv()
	{
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
		EnsureBound(); // 세션 Mailbox 보장

		// 1) 내 샤드(세션 routeKey 기준)에서 로컬 가입 등록
		const uint64 myShard_id = m_dir->PickShard(m_key.value());
		auto my_shard = m_dir->ShardAt(myShard_id);
		if (my_shard) 
		{
			auto q = m_mbNorm; // Normal 채널 수신
			my_shard->Submit(utils::job::Job([s = my_shard, group_id, q] {
					s->OnGroupLocalJoin(group_id, q);
				}));
		}

		// 2) 홈 샤드에 “내 샤드에 멤버 +1” 통지 (Ctrl 채널 권장)
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

		// 1) 내 샤드에서 로컬 제거
		const uint64 myShard_id = m_dir->PickShard(m_key.value());
		auto my_shard = m_dir->ShardAt(myShard_id);
		if (my_shard) 
		{
			auto q = m_mbNorm;
			my_shard->Submit(utils::job::Job([s = my_shard, group_id, q] {
					s->OnGroupLocalLeave(group_id, q);
				}));
		}

		// 2) 홈 샤드 refcnt -1
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

		const uint64 shard_id = m_dir->PickShard(m_key.value());
		auto shard = m_dir->ShardAt(shard_id);
		if (!shard) return;

		// 세션용 Mailbox 2채널 생성
		auto qN = shard->CreateMailbox(utils::exec::eMailboxChannel::NORMAL);
		auto qC = shard->CreateMailbox(utils::exec::eMailboxChannel::CTRL);

		m_mbNorm = std::move(qN);
		m_mbCtrl = std::move(qC);
		m_boundShard = shard;

		// 엔드포인트 생성 시 ECS 엔티티를 샤드 로컬에 생성해 컴포넌트 구성
		if (m_entitiy != entt::null) return;
		shard->Local().defers.emplace_back([this](entt::registry& r)
			{
				if (m_entitiy != entt::null) return;

				m_entitiy = r.create();

				// 세션/메일박스/라우팅키 연결
				r.emplace<ecs::SessionRef>(m_entitiy, ecs::SessionRef{ m_session });
				r.emplace<ecs::MailboxRef>(m_entitiy, ecs::MailboxRef{ m_mbNorm, m_mbCtrl });
				r.emplace<utils::exec::RouteKey>(m_entitiy, utils::exec::RouteKey{ m_key });

				// 네트워크 ECS 컴포넌트 기본 셋업
				r.emplace<ecs::CompReliability>(m_entitiy);
				r.emplace<ecs::CompFragment>(m_entitiy);
				r.emplace<ecs::CompChannel>(m_entitiy);
				r.emplace<ecs::CompNetstat>(m_entitiy);
				r.emplace<ecs::CompHandshake>(m_entitiy);
				r.emplace<ecs::CompCongestion>(m_entitiy);

				// 필요 시 추가 컴포넌트도 여기서 부착
				// r.emplace<GroupMember>(...);
			});
	}

	void SessionEndpoint::RebindIfExecutorChanged()
	{
		// 키 기준으로 현재 샤드 재계산
		const uint64 sid = m_dir->PickShard(m_key.value());	//
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

		// 1) 세션 전용 Mailbox로 시도 (빠른 경로)
		EnsureBound();
		auto& q = (ch == utils::exec::eMailboxChannel::NORMAL) ? m_mbNorm : m_mbCtrl;
		if (q && q->Post(std::move(j)))
			return;

		// 2) 실패 시: 최신 엔드포인트 재획득 + 실행자 재바인딩 후 "한 번만" Post
		auto& ep = (ch == utils::exec::eMailboxChannel::NORMAL) ? m_epNorm : m_epCtrl;
		ep = m_dir->EndpointFor(m_key, ch);
		RebindIfExecutorChanged();
		(void)ep.Post(std::move(j));
	}


}
