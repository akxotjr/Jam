#pragma once
#include "ShardDirectory.h"
#include "RoutingPolicy.h"

namespace jam::net
{


	class SessionEndpoint
	{
    public:
        SessionEndpoint(utils::exec::ShardDirectory& dir, utils::exec::RouteKey key);

        // 일반 작업
        void Post(utils::job::Job j);

        // 제어/지연민감
        void PostCtrl(utils::job::Job j);


        // 세션 이동/리바인딩(샤드가 바뀌는 경우->migrate)
        void RebindKey(utils::exec::RouteKey newKey);

        // 세션 단위 드레인(새로운 Post 차단)
        void BeginDrain();


        // Group Routing
        void JoinGroup(uint64 group_id, utils::exec::GroupHomeKey gk);
        void LeaveGroup(uint64 group_id, utils::exec::GroupHomeKey gk);
        void PostGroup(uint64 group_id, utils::exec::GroupHomeKey gk, utils::job::Job j);


    private:
        void RefreshEnpoint();
        void EnsureBound();     // lazy-bind

        void RebindIfExecutorChanged();

        void PostImpl(utils::job::Job j, utils::exec::eMailboxChannel ch);


    private:
        USE_LOCK

        utils::exec::RouteKey                       m_key;

        utils::exec::ShardDirectory*                m_dir = nullptr;   
        std::atomic<bool>                           m_closed{ false };

        // 채널별 슬롯 엔드포인트(슬롯/세대 스냅샷 포함)
        utils::exec::ShardEndpoint                  m_epNorm{nullptr};
        utils::exec::ShardEndpoint                  m_epCtrl{nullptr};

        Sptr<utils::exec::Mailbox>                  m_mbNorm;
        Sptr<utils::exec::Mailbox>                  m_mbCtrl;
        Wptr<utils::exec::ShardExecutor>            m_boundShard;

        //ecs-temp
        entt::entity            m_entitiy{ entt::null };
        std::weak_ptr<Session>  m_session;
	};

}
