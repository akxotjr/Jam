#include "pch.h"
#include "Session.h"

namespace jam::net
{
	void Session::AttachEndpoint(utils::exec::ShardDirectory& dir, utils::exec::RouteKey routeKey)
	{
		m_endpoint = std::make_unique<SessionEndpoint>(dir, routeKey);
		m_endpoint->BindSession(static_pointer_cast<Session>(shared_from_this()));
	}

	void Session::RebindRouteKey(utils::exec::RouteKey newKey)
	{
		if (m_endpoint) m_endpoint->RebindKey(newKey);
	}

	void Session::Post(utils::job::Job j)
	{
		if (m_endpoint) m_endpoint->Post(std::move(j));
	}

	void Session::PostCtrl(utils::job::Job j)
	{
		if (m_endpoint) m_endpoint->PostCtrl(std::move(j));
	}

	// GE 타이머 만료 시 해당 세션 routeKey 샤드로 Job Post
	void Session::PostAfter(uint64 delay_ns, utils::job::Job j)
	{
		auto srvc = GetService();
		if (!srvc || !m_endpoint)
			return;

		auto* ge = srvc->GetGlobalExecutor();
		if (!ge)
			return;

		ge->PostAfter(utils::job::Job([ep = m_endpoint.get(), jj = std::move(j)]() mutable
			{
				if (ep) ep->Post(std::move(jj));
			}), delay_ns);
	}

	void Session::JoinGroup(uint64 group_id, utils::exec::GroupHomeKey gk)
	{
		if (m_endpoint) m_endpoint->JoinGroup(group_id, gk);
	}

	void Session::LeaveGroup(uint64 group_id, utils::exec::GroupHomeKey gk)
	{
		if (m_endpoint) m_endpoint->LeaveGroup(group_id, gk);
	}

	void Session::PostGroup(uint64 group_id, utils::exec::GroupHomeKey gk, utils::job::Job j)
	{
		if (m_endpoint) m_endpoint->PostGroup(group_id, gk, std::move(j));
	}
}


