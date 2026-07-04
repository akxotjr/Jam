#pragma once

#include "jamnet/core/executor/Lock.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/NetAddress.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/runtime/world/types/WorldActionTypes.h"
#include "jamnet/runtime/ServerSession.h"
#include "jamnet/runtime/UserContext.h"

#include <jampx/IPhysicsFacade.h>

#include <memory>
#include <string>

namespace jam::net
{
	enum class eProtocolType : uint8;
	class ServerService;
	class ServerTcpSession;
	class ServerUdpSession;
	class ServerWorldActionSystem;
	class WorldBase;

	using PhysicsFactory = std::function<std::unique_ptr<px::IPhysicsFacade>()>;
	using PhysicsFactoryProvider = std::function<std::unique_ptr<px::IPhysicsFacade>(const std::string& physicsAssetPath)>;


	struct ServerConfig
	{
		NetAddress				tcpAddress		= { "127.0.0.1", 7777 };
		NetAddress				udpAddress		= { "127.0.0.1", 8888 };
		uint32					maxConnections  = 1000;

		PhysicsFactory			physicsFactory	= nullptr;
		PhysicsFactoryProvider	physicsFactoryProvider = nullptr;

		std::string				sharedDataCatalogPath;
		std::string				worldTemplatePath;
		std::string				worldArchetypePath;
	};

	class ServerNetworkManager
	{
		friend class ServerWorldActionSystem;

	public:
		explicit ServerNetworkManager(const ServerConfig& config);
		~ServerNetworkManager();

		bool								Start();
		void								Stop();
		bool								IsRunning() const { return m_running.load(std::memory_order_acquire); };

		ServerWorldActionSystem*			GetWorldActionSystem() const { return m_worldActionSystem.get(); }
		void								RequestWorldAction(WorldActionRequest req);

		std::shared_ptr<ServerService>		GetService() const { return m_service; }

		PhysicsFactory						GetPhysicsFactory() const { return m_config.physicsFactory; }
		PhysicsFactoryProvider				GetPhysicsFactoryProvider() const { return m_config.physicsFactoryProvider; }

		void								CreateWorld(WorldKey key);
		void								DestroyWorld(const WorldKey& key);

		bool								SubmitWorldJob(const WorldKey& key, std::function<void(WorldBase&)> job);

		bool								CacheTcpSession(UserId userId, ServerTcpSession* tcp);
		bool								CacheUdpSession(UserId userId, ServerUdpSession* udp);
		void								ReleaseUdpSession(UserId userId, const ServerUdpSession* udp);
		void								ReleaseSession(UserId userId);
		ServerSessionBundle					GetSessionBundle(UserId userId);

		ServerTcpSession*					FindTcpSession(UserId userId);
		ServerUdpSession*					FindUdpSession(UserId userId);

		void								Send(UserId userId, Packet packet, eProtocolType protocol);
		void								Multicast(const WorldKey& key, Packet packet);
		void								Broadcast(Packet packet);

	private:
		bool								StartServerService();
		void								StopServerService();

	private:
		static constexpr int	kSessionLockIdx	 = 1;
		USE_MANY_LOCKS(2)

		ServerConfig										m_config			= {};

		std::shared_ptr<ServerService>						m_service			= nullptr;
		std::unique_ptr<ServerWorldActionSystem>			m_worldActionSystem	= nullptr;

		std::atomic<bool>									m_running			= false;

		std::unordered_map<UserId, ServerSessionBundle>		m_sessions;		// UserId : SessionRefBundle
	};
}
