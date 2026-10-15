#pragma once

#include "jamnet/runtime/session/UserContext.h"
#include <atomic>
#include <memory>

namespace jam::net
{
	class ServerNetworkManager;

	// Synchronizes an already committed membership; failures never roll it back.
	class ServerWorldSyncSystem : public std::enable_shared_from_this<ServerWorldSyncSystem>
	{
	public:
		void			Initialize(ServerNetworkManager* owner) { m_owner = owner; }
		void			Close() { m_owner = nullptr; }
		void			BeginWorldSync(UserContext& user);
		static void		ResetWorldSync(UserContext& user);
		void			TryCompleteWorldSync(UserContext& user);
		void			OnClientWorldSyncResult(UserId userId, const ClientWorldSyncResult& result, Timestamp now);
		void			Tick(Timestamp now);

	private:
		UserContext*	FindUserOnCurrentShard(UserId userId) const;
		WorldSyncToken	IssueWorldSyncToken();
		void			OnWorldSyncActivated(UserId userId, const UserWorldSyncContext& request, bool succeeded);
		void			FailWorldSync(UserContext& user);

	private:
		ServerNetworkManager*	m_owner					= nullptr;
		std::atomic<uint64>		m_nextWorldSyncToken	= 1;
		Duration				m_worldSyncTimeoutNs	= 15_s;
	};
}
