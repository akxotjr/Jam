#pragma once

#include "jamnet/core/executor/Lock.h"
#include "jamnet/core/executor/ExecutorPeriodic.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/NetAddress.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"
#include "jamnet/runtime/world/lifecycle/WorldActionTypes.h"
#include "jamnet/runtime/world/lifecycle/ServerWorldTransitionCoordinator.h"
#include "jamnet/runtime/world/data/SharedDataManifest.h"
#include "jamnet/runtime/session/ServerSession.h"
#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/social/SocialTypes.h"


#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace jam
{
	class ShardExecutor;
}

namespace jam::net
{
	class IServerSocialContent;
	class SocialService;
	enum class eProtocolType : uint8;
	class ServerService;
	class ServerTcpSession;
	class ServerUdpSession;
	class WorldBase;
	class IServerWorldContent;

	struct ServerConfig
	{
		using WorldContentFactory = std::function<std::unique_ptr<IServerWorldContent>(const WorldConfig&)>;

		NetAddress				tcpAddress		= { "127.0.0.1", 7777 };
		NetAddress				udpAddress		= { "127.0.0.1", 8888 };
		uint32					maxConnections  = 1000;
		std::string				sharedDataManifestPath;
		WorldContentFactory		worldContentFactory;
		std::shared_ptr<IServerSocialContent> socialContent = nullptr;
	};

	class ServerNetworkManager
	{
		friend class ServerWorldTransitionCoordinator;

	public:
		explicit ServerNetworkManager(const ServerConfig& config);
		~ServerNetworkManager();

		bool								Start();
		void								Stop();
		bool								IsRunning() const { return m_running.load(std::memory_order_acquire); };

		ServerWorldTransitionCoordinator*	GetWorldTransitionSystem() const { return m_worldTransitions.get(); }
		void								EnterWorld(UserId userId, const EnterWorldRequest& request);
		void								LeaveWorld(UserId userId, const LeaveWorldRequest& request);
		bool								DispatchSocialCommand(UserId userId, SocialCommand command);

		std::shared_ptr<ServerService>		GetService() const { return m_service; }
		const SharedDataManifest*			GetSharedDataManifest() const { return &m_manifest; }
		std::unique_ptr<IServerWorldContent>	CreateWorldContent(const WorldConfig& config) const;

		void								BootstrapWorldInstances(std::function<void(bool)> completed);
		void								DestroyWorld(const WorldRuntimeRef& runtime);

		bool								SubmitWorldJob(const WorldRuntimeRef& runtime, std::function<void(WorldBase&)> job);

		bool								CacheTcpSession(UserId userId, ServerTcpSession* tcp);
		bool								CacheUdpSession(UserId userId, ServerUdpSession* udp);
		void								ReleaseUdpSession(UserId userId, const ServerUdpSession* udp);
		void								ReleaseSession(UserId userId, const ServerTcpSession* tcp);
		ServerSessionBundle					GetSessionBundle(UserId userId);

		ServerTcpSession*					FindTcpSession(UserId userId);
		ServerUdpSession*					FindUdpSession(UserId userId);

		void								Send(UserId userId, Packet packet, eProtocolType protocol);
		void								Multicast(const WorldRuntimeRef& runtime, Packet packet);
		void								Broadcast(Packet packet);

	private:
		bool								StartServerService();
		void								StopServerService();
		void								StartWorldTransitionTicks();
		void								StopWorldTransitionTicks();

	private:
		static constexpr int	kSessionLockIdx	 = 1;
		USE_MANY_LOCKS(2)

		ServerConfig										m_config			= {};
		SharedDataManifest									m_manifest			= {};

		std::shared_ptr<ServerService>						m_service			= nullptr;
		std::shared_ptr<ServerWorldTransitionCoordinator>	m_worldTransitions	= nullptr;
		std::shared_ptr<SocialService>						m_socialService		= nullptr;
		std::vector<std::pair<std::shared_ptr<ShardExecutor>, PeriodicHandle>> m_worldTransitionTicks;

		std::atomic<bool>									m_running			= false;

		std::unordered_map<UserId, ServerSessionBundle>		m_sessions;		// UserId : SessionRefBundle
	};
}
