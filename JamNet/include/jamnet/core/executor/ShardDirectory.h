#pragma once
#include "jamnet/core/executor/ShardRoutingPolicy.h"
#include "jamnet/core/executor/ShardSlot.h"  
#include "jamnet/core/executor/ShardExecutor.h"

#include <mutex>


namespace jam
{

	struct ShardDirectoryConfig
	{
		uint32					        numShards           = 0;
		ShardExecutorConfig		        shardCfg            = {};
		RouteSeed						routeSeed			= {};
		bool                            pinShardWorkers     = true;
		std::vector<ThreadAffinitySlot> affinitySlots;
		uint32                          affinitySlotOffset  = 0;
	};


	class ShardDirectory : public std::enable_shared_from_this<ShardDirectory>
	{
	public:
		ShardDirectory(const ShardDirectoryConfig& cfg) : m_config(cfg), m_routing(cfg.routeSeed) {}
		~ShardDirectory();

		void                                            Init();
														
		void                                            Start();            
		void                                            StopAll() const;    
		void                                            JoinAll() const;    
														
		// Slot binding                                 
		void                                            AttachSlots();     

		// 라우팅 / 조회
		uint64                                          Size() const;
		RouteKey										MakeRouteKey(RouteDomain domain, uint64 id) const { return m_routing.MakeKey(domain, id); }
		RouteKey										MakeRouteKey(std::string_view domain, uint64 id) const { return m_routing.MakeKey(domain, id); }
		uint64                                          PickShard(RouteKey key) const;
		RouteAssignment                                 PlaceRoute(RouteKey key, const RoutePlacementOptions& opt) const;
		void                                            ReleaseRoute(const RouteAssignment& assignment) const;
		std::shared_ptr<ShardExecutor>                  ShardAt(uint64 index) const;
		std::vector<std::shared_ptr<ShardExecutor>>&    Shards() { return m_shards; }

		std::shared_ptr<ShardExecutor>                  ShardFor(RouteKey key) const { return ShardAt(PickShard(key)); }

	private:
		void                                            AssignCoreSlots();

	private:

		ShardDirectoryConfig                            m_config = {};
		ShardRoutingPolicy								m_routing;
		std::vector<std::shared_ptr<ShardExecutor>>     m_shards;
		std::vector<ShardSlot>                          m_slots;
		mutable std::mutex                              m_routePlacementMutex;
		mutable std::vector<uint64>                     m_routePlacementCounts;
	};
}
