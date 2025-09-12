#pragma once
#include "ShardDirectory.h"
#include "RoutingPolicy.h"

namespace jam::net
{


	class SessionEndpoint
	{
    public:
        SessionEndpoint(utils::exec::ShardDirectory& dir, utils::exec::RouteKey key);

        // �Ϲ� �۾�
        void Post(utils::job::Job j);

        // ����/�����ΰ�
        void PostCtrl(utils::job::Job j);


        // ���� �̵�/�����ε�(���尡 �ٲ�� ���->migrate)
        void RebindKey(utils::exec::RouteKey newKey);

        // ���� ���� �巹��(���ο� Post ����)
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
	                    ev.e = e;                         // ��ƼƼ ����
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
