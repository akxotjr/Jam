#pragma once
#include "ShardDirectory.h"
#include "RoutingKey.h"

namespace jam::net
{


	class SessionEndpoint
	{
    public:
        SessionEndpoint(utils::exec::ShardDirectory& dir, uint64 routeKey, utils::exec::RouteSeed seed);

        // �Ϲ� �۾�
        void Post(utils::job::Job j);

        // ����/�����ΰ�
        void PostCtrl(utils::job::Job j);


        void PostGroup(uint64 group_id, utils::job::Job j);

        // ���� �̵�/�����ε�(���尡 �ٲ�� ���->migrate)
        void RebindKey(uint64 newKey);

        // ���� ���� �巹��(���ο� Post ����)
        void BeginDrain();


        // Routing
        void JoinGroup(uint64 group_id);
        void LeaveGroup(uint64 group_id);


    private:
        void RefreshEnpoint();
        void EnsureBound();     // lazy-bind

        void RebindIfExecutorChanged();

        void PostImpl(utils::job::Job j, utils::exec::eMailboxChannel ch);


    private:
        USE_LOCK

		utils::exec::RoutingKey                     m_routing;


        utils::exec::ShardDirectory*                m_dir = nullptr;   
        std::atomic<bool>                           m_closed{ false };

        // ä�κ� ���� ��������Ʈ(����/���� ������ ����)
        utils::exec::ShardEndpoint                  m_epNorm{nullptr};
        utils::exec::ShardEndpoint                  m_epCtrl{nullptr};

        Sptr<utils::exec::Mailbox>                  m_mbNorm;
        Sptr<utils::exec::Mailbox>                  m_mbCtrl;
        Wptr<utils::exec::ShardExecutor>            m_boundShard;

	};

}
