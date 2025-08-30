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
		uint32					numShards = 0;          // Own 모드에서만 사용
		ShardExecutorConfig		shardCfg;             // 공통 설정 (Own 모드)
	};


	class ShardDirectory : public enable_shared_from_this<ShardDirectory>
	{
	public:
        ShardDirectory(const ShardDirectoryConfig& cfg, std::weak_ptr<GlobalExecutor> owner);
        ~ShardDirectory();


        void Init(const std::vector<Sptr<ShardExecutor>>& shards);

        void Start();      // 샤드 생성+시작(ownership==Own일 때만 의미)
        void StopAll();    // Own일 때만 샤드 정지
        void JoinAll();    // Own일 때만 조인

        // Slot binding
        void AttachSlots();     

        // 라우팅 / 조회
        uint64                  Size() const;
        uint64                  PickShard(uint64 key) const;
        Sptr<ShardExecutor>     ShardAt(uint64 i) const;

        // 엔드포인트 발급
        ShardEndpoint           EndpointFor(uint64 key) const;
        ShardEndpoint           EndpointFor(uint64 key, eMailboxChannel channel) const;
        ShardEndpoint           EndpointFor(RouteKey rk, eMailboxChannel channel) const;
        ShardEndpoint           EndpointFor(GroupHomeKey gk, eMailboxChannel channel) const;

    private:
        ShardDirectoryConfig                m_config{};
        std::weak_ptr<GlobalExecutor>       m_owner;
        std::vector<Sptr<ShardExecutor>>    m_shards;   // Own/Adopt 공통 보관
        std::vector<ShardSlot>              m_slots;    
	};


}
