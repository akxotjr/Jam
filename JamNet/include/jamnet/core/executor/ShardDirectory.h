#pragma once
#include "jamnet/core/executor/RoutingPolicy.h"
#include "jamnet/core/executor/ShardSlot.h"  
#include "jamnet/core/executor/ShardExecutor.h"


namespace jam
{

	struct ShardDirectoryConfig
	{
		int32					numShards = 0;        
		ShardExecutorConfig		shardCfg  = {};
	};


	class ShardDirectory : public std::enable_shared_from_this<ShardDirectory>
	{
	public:
        ShardDirectory(const ShardDirectoryConfig& cfg) : m_config(cfg) {}
        ~ShardDirectory();


        void                                            Init();
                                                        
        void                                            Start();            
        void                                            StopAll() const;    
        void                                            JoinAll() const;    
                                                        
        // Slot binding                                 
        void                                            AttachSlots();     

        // 라우팅 / 조회
        uint64                                          Size() const;
        uint64                                          PickShard(uint64 key) const;
        std::shared_ptr<ShardExecutor>                  ShardAt(uint64 index) const;
        std::vector<std::shared_ptr<ShardExecutor>>&    Shards() { return m_shards; }

        std::shared_ptr<ShardExecutor>                  ShardFor(RouteKey key) const { return ShardAt(PickShard(key.value())); }


    private:
        ShardDirectoryConfig                            m_config = {};
        std::vector<std::shared_ptr<ShardExecutor>>     m_shards;
        std::vector<ShardSlot>                          m_slots;    
	};
}
