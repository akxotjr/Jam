#pragma once

#include "jamnet/runtime/content/social/ISocialDelivery.h"

#include <atomic>
#include <memory>

namespace jam
{
	class ShardExecutor;
}

namespace jam::net
{
	class ISocialContent;
	class ServerNetworkManager;

	class SocialService final : public ISocialDelivery, public std::enable_shared_from_this<SocialService>
	{
	public:
		bool	Initialize(ServerNetworkManager* owner, std::shared_ptr<ISocialContent> content);
		void	Shutdown();

		bool	Submit(const SocialPrincipal& sender, SocialCommand command);
		void	NotifyConnected(SocialPrincipal principal);
		void	NotifyDisconnected(UserId userId);

		void	SendTo(UserId userId, const SocialMessage& message) override;
		void	SendToWorld(const WorldRef& world, const SocialMessage& message) override;
		void	Broadcast(const SocialMessage& message) override;

	private:
		ServerNetworkManager*				m_owner		= nullptr;
		std::shared_ptr<ISocialContent>		m_content	= nullptr;
		std::shared_ptr<ShardExecutor>		m_shard		= nullptr;
		std::atomic<bool>					m_running	= false;
	};
}
