#pragma once

#include "jamnet/core/executor/ExecutorPeriodic.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/IAuthenticator.h"
#include "jamnet/core/net/NetAddress.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"
#include "jamnet/runtime/world/lifecycle/WorldActionTypes.h"
#include "jamnet/runtime/world/lifecycle/ServerWorldTransitionCoordinator.h"
#include "jamnet/runtime/world/data/SharedDataManifest.h"
#include "jamnet/runtime/session/ServerSession.h"
#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/content/social/SocialTypes.h"
#include "jamnet/runtime/content/generic/GenericContentTypes.h"


#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace jam
{
	class ShardExecutor;
}

namespace jam::net
{
	class ISocialContent;
	class SocialService;
	class IGenericContent;
	class GenericContentService;
	enum class eProtocolType : uint8;
	class ServerService;
	class ServerTcpSession;
	class ServerUdpSession;
	class WorldBase;
	class IWorldContent;

	struct ServerConfig
	{
		using WorldContentFactory = std::function<std::unique_ptr<IWorldContent>(const WorldConfig&)>;
		using EnterWorldDestinationResolver = std::function<std::optional<WorldArchetypeKey>(AccountId, UserId)>;

		NetAddress				tcpAddress		= { "127.0.0.1", 7777 };
		NetAddress				udpAddress		= { "127.0.0.1", 8888 };
		uint32					maxConnections  = 1000;
		uint64					reconnectTimeoutNs = 15'000'000'000ull;
		std::string				sharedDataManifestPath;
		WorldContentFactory		worldContentFactory;
		EnterWorldDestinationResolver enterWorldDestinationResolver;
		std::shared_ptr<ISocialContent>			socialContent = nullptr;
		std::shared_ptr<IAuthenticator>			authenticator = nullptr;
		std::shared_ptr<IGenericContent>					content = nullptr;
	};

	class ServerNetworkManager
	{
		friend class ServerWorldTransitionCoordinator;

	public:
		explicit ServerNetworkManager(const ServerConfig& config);
		~ServerNetworkManager();

		bool									Start();
		void									Stop();
		bool									IsRunning() const { return m_running.load(std::memory_order_acquire); };

		ServerWorldTransitionCoordinator*		GetWorldTransitionSystem() const { return m_worldTransitions.get(); }
		void									EnterWorld(UserId userId, const EnterWorldRequest& request);
		void									LeaveWorld(UserId userId, const LeaveWorldRequest& request);
		bool									DispatchSocialCommand(UserId userId, SocialCommand command);
		bool									DispatchContentRequest(UserId userId, GenericContentRequest request);
		std::shared_ptr<ServerService>			GetService() const { return m_service; }
		const SharedDataManifest*				GetSharedDataManifest() const { return &m_manifest; }
		uint64									GetReconnectTimeoutNs() const { return m_config.reconnectTimeoutNs; }
		std::optional<WorldArchetypeKey>		ResolveEnterWorldDestination(AccountId accountId, UserId userId) const;
		std::unique_ptr<IWorldContent>	CreateWorldContent(const WorldConfig& config) const;

		void									BootstrapWorldInstances(std::function<void(bool)> completed);
		void									DestroyWorld(const WorldRef& world);

		void									ReleaseSession(UserId userId, const ServerTcpSession* tcp);
		void									NotifyUserConnected(const UserContext& user);

	private:
		bool									StartServerService();
		void									StopServerService();
		void									StartWorldTransitionTicks();
		void									StopWorldTransitionTicks();
		void									NotifyUserReleased(UserId userId);

	private:
		ServerConfig										m_config			= {};
		SharedDataManifest									m_manifest			= {};

		std::shared_ptr<ServerService>						m_service			= nullptr;
		std::shared_ptr<ServerWorldTransitionCoordinator>	m_worldTransitions	= nullptr;
		std::shared_ptr<SocialService>						m_socialService		= nullptr;
		std::shared_ptr<GenericContentService>				m_contentService	= nullptr;

		std::vector<std::pair<std::shared_ptr<ShardExecutor>, PeriodicHandle>> m_worldTransitionTicks;

		std::atomic<bool>									m_running			= false;
	};
}
