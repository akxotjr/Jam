#pragma once
#include "Components.h"
#include "EcsEvents.h"
#include "ReliableTransportManager.h"


namespace jam::net::ecs
{

#pragma region Reliability System

	struct ReliabilitySinks
	{
		bool wired = false;

		entt::scoped_connection onAck;
		//entt::scoped_connection onNack;   // 필요 시
		//entt::scoped_connection onData;   // 필요 시
		//entt::scoped_connection onPiggyFail;
	};


	struct ReliabilityHandlers
	{
		void OnRecvAck(entt::registry& R, const EvRecvAck& ev)
		{
			
		}
	};


	static inline bool SeqGreater(uint16 a, uint16 b) { return static_cast<int16>(a - b) > 0; }

	static inline uint32 BuildAckBitfield(const ReliabilityState& r, uint16 latestSeq) {
		uint32 bf = 0;
		for (uint16 i = 1; i <= BITFIELD_SIZE; ++i) {
			uint16 seq = latestSeq - i;
			if (!SeqGreater(latestSeq, seq)) continue;
			if (r.receiveHistory.test(seq % WINDOW_SIZE)) bf |= (1u << (i - 1));
		}
		return bf;
	}



	void ReliabilityWiringSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);
	void ReliabilityTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);

#pragma endregion


#pragma region NetStat System

	void NetStatSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);

#pragma endregion

#pragma region CongestionControl System

	void CongestionSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);

#pragma endregion

#pragma region Fragment System
	void FragmentSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);
#pragma endregion

#pragma region Channel System
	void ChannelSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);
#pragma endregion


	void GroupHomeSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);

}
