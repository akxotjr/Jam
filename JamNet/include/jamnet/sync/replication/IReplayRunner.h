#pragma once

#include "jamnet/sync/replication/NetActorComponents.h"

namespace jam::net
{
	
	struct ReplayContext
	{
		uint64			tick		= 0;
		entt::entity	local		= entt::null;
		uint32			inputAck	= 0;
	};


	class IReplayRunner
	{
	public:
		virtual ~IReplayRunner() = default;

		virtual void Prepare(entt::registry& world, const ReplayContext& ctx) = 0;
		virtual void Run(entt::registry& world, const ReplayContext& ctx) = 0;
		virtual void Commit(entt::registry& world, const ReplayContext& ctx) = 0;
	};

} // namespace jam::net
