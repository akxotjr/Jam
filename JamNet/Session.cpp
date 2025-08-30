#include "pch.h"
#include "Session.h"

namespace jam::net
{
	void Session::AttachEndpoint(utils::exec::ShardDirectory& dir, uint64 routeKey, utils::exec::RouteSeed seed)
	{
		m_endpoint = std::make_unique<SessionEndpoint>(dir, routeKey, seed);
	}

	void Session::RebindRouteKey(uint64 newKey)
	{
		if (m_endpoint) m_endpoint->RebindKey(newKey);
	}
}


