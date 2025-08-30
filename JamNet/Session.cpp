#include "pch.h"
#include "Session.h"

namespace jam::net
{
	void Session::AttachEndpoint(utils::exec::ShardDirectory& dir, utils::exec::RouteKey routeKey)
	{
		m_endpoint = std::make_unique<SessionEndpoint>(dir, routeKey);
	}

	void Session::RebindRouteKey(utils::exec::RouteKey newKey)
	{
		if (m_endpoint) m_endpoint->RebindKey(newKey);
	}
}


