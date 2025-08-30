#pragma once
#include "RoutingPolicy.h"
#include "ShardEndpoint.h"
#include "ShardExecutor.h"


namespace jam::utils::exec
{
	enum class eShardOwnership : uint8
	{
		OWN,
		ADOPT
	};

	struct ShardDirectoryConfig
	{
		eShardOwnership			ownership = eShardOwnership::OWN;
		uint32					numShards = 0;          // Own ��忡���� ���
		ShardExecutorConfig		shardCfg;             // ���� ���� (Own ���)
	};


	class ShardDirectory : public enable_shared_from_this<ShardDirectory>
	{
	public:
        ShardDirectory(const ShardDirectoryConfig& cfg, std::weak_ptr<GlobalExecutor> owner);
        ~ShardDirectory();


        void Init(const std::vector<Sptr<ShardExecutor>>& shards);

        void Start();      // ���� ����+����(ownership==Own�� ���� �ǹ�)
        void StopAll();    // Own�� ���� ���� ����
        void JoinAll();    // Own�� ���� ����

        // Slot binding
        void AttachSlots();     

        // ����� / ��ȸ
        uint64                  Size() const;
        uint64                  PickShard(uint64 key) const;
        Sptr<ShardExecutor>     ShardAt(uint64 i) const;

        // ��������Ʈ �߱�
        ShardEndpoint           EndpointFor(uint64 key) const;
        ShardEndpoint           EndpointFor(uint64 key, eMailboxChannel channel) const;
        ShardEndpoint           EndpointFor(RouteKey rk, eMailboxChannel channel) const;
        ShardEndpoint           EndpointFor(GroupHomeKey gk, eMailboxChannel channel) const;

    private:
        ShardDirectoryConfig                m_config{};
        std::weak_ptr<GlobalExecutor>       m_owner;
        std::vector<Sptr<ShardExecutor>>    m_shards;   // Own/Adopt ���� ����
        std::vector<ShardSlot>              m_slots;    
	};


}
