#pragma once

#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/NetAddress.h"
#include "jamnet/core/executor/MailboxRef.h"
#include "jamnet/runtime/world/core/WorldDirectory.h"
#include "jamnet/sync/networld/ClientPhysicalWorld.h"


#include <jampx/IPhysicsFacade.h>



namespace jam::net
{
	class ClientService;
	class ClientTcpSession;
	class ClientUdpSession;
	class ClientWorldActionSystem;

	struct ClientConfig
	{
		AccountId		accountId			= kInvalidAccountId;
		NetAddress      serverTcpAddress    = { "127.0.0.1", 7777 };
		NetAddress      serverUdpAddress    = { "127.0.0.1", 8888 };

		using PhysicsFactory = std::function<std::unique_ptr<px::IPhysicsFacade>()>;
		PhysicsFactory  physicsFactory      = nullptr;

		std::string		sharedDataCatalogPath;
		std::string		worldTemplatePath;
		std::string		worldArchetypePath;
		bool            headlessWorld       = false;
	};

	class ClientNetworkManager
	{
		friend class ClientTcpSession;
		friend class ClientUdpSession;
		friend class ClientWorldActionSystem;

	public:
		explicit ClientNetworkManager(const ClientConfig& config, AccountId accountId);
		~ClientNetworkManager();

		bool                                    Connect();
		void                                    Disconnect();
		bool                                    IsConnected() const;
		bool                                    IsTcpConnected() const;
		bool                                    IsUdpConnected() const;

		ClientTcpSession*                       GetTcpSession() const { return m_tcp.TryGetRaw(); }
		ClientUdpSession*                       GetUdpSession() const { return m_udp.TryGetRaw(); }
		ClientWorldActionSystem*				GetWorldActionSystem() const { return m_worldActionSystem.get(); }

		AccountId								GetAccountId() const { return m_accountId; }
		UserId                                  GetUserId() const { return m_userId.load(std::memory_order_acquire); }

		void									RequestWorldAction(eWorldAction action, const WorldKey& src = {}, const WorldKey& target = {});
		void									RequestSpawnActor(LocalWorldId worldId, const SpawnParams& params);
		void									RequestDespawnActor(LocalWorldId worldId, NetId netId);
		void									RequestPossessActor(LocalWorldId worldId, NetId netId);
		void									RequestUnpossessActor(LocalWorldId worldId, NetId netId);
		void									PushInput(LocalWorldId worldId, uint32 inputFlags, float pitch, float yaw, uint32 commandEpoch);
		void									PushInput(LocalWorldId worldId, const px::CharacterInput& input);
		void									SetLatestClickMoveSeq(LocalWorldId worldId, uint64 requestSeq);
		void									RequestClickMove(LocalWorldId worldId, const px::Vec3& from, const px::Vec3& dir, float maxRange, uint64 requestSeq, uint32 commandEpoch, float facingYaw);


	protected:
		bool                                    StartClientService();
		void                                    StopClientService();

		bool                                    ConnectTcp();
		bool                                    ConnectUdp();

		void                                    NotifyTcpBound(UserId userId);
		void                                    NotifyUdpBound(UserId userId);

	private:
		void									SubmitUserContextUpdate(std::function<void(UserContext&)> fn);
		uint16									ResolveClientUserShardIndex() const;

		void									PublishNetworkStateEvent() const;
		void                                    UpdateSessionReadyState();

	private:
		ClientConfig                            m_config					= {};
		AccountId								m_accountId					= kInvalidAccountId;
		std::atomic<UserId>						m_userId					= kInvalidUserId;

		std::shared_ptr<ClientService>           m_service					= nullptr;
		SessionRef<ClientTcpSession>             m_tcp						= {};
		SessionRef<ClientUdpSession>             m_udp						= {};
		std::unique_ptr<ClientWorldActionSystem> m_worldActionSystem		= nullptr;

		std::atomic_bool                        m_running					= false;

		std::atomic_bool                        m_tcpBound					= false;
		std::atomic_bool                        m_udpBound					= false;
		std::atomic_bool                        m_sessionReady				= false;

	};
}
