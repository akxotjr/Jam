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

        // ä�κ� ���� ��������Ʈ(����/���� ������ ����)
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
