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

        void BindSession(std::weak_ptr<Session> s);


        // Emit Helpers

        template<typename Ev>
        void Emit(Ev ev)
        {
            PostCtrl(utils::job::Job([wk = m_boundShard, ev = std::move(ev), e = m_entitiy]() mutable {
	                if (auto sh = wk.lock())
	                {
	                    auto& L = sh->Local();            // ShardLocal
	                    ev.e = e;                         // 엔티티 주입
	                    L.events.enqueue<Ev>(std::move(ev));
	                }
                }));
        }

        
        void EmitConnect();
        void EmitDisconnect();
        void EmitSend();
        void EmitRecv();

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

        utils::exec::ShardEndpoint                  m_epNorm{nullptr};
        utils::exec::ShardEndpoint                  m_epCtrl{nullptr};

        Sptr<utils::exec::Mailbox>                  m_mbNorm;
        Sptr<utils::exec::Mailbox>                  m_mbCtrl;
        Wptr<utils::exec::ShardExecutor>            m_boundShard;

        entt::entity                                m_entitiy{ entt::null };
        std::weak_ptr<Session>                      m_session;
	};

}
