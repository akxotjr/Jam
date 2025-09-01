#pragma once


namespace jam::net::ecs
{
	void SessionUpdateSystem(struct utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);
}
