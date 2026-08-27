#pragma once

#include "jamnet/core/net/PacketStructure.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/NetAddress.h"
#include "jamnet/runtime/session/ClientPrincipalState.h"
#include "jamnet/runtime/world/data/WorldTemplateDatabase.h"
#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"
#include "jamnet/runtime/world/data/WorldConfigResolver.h"
#include "jamnet/runtime/world/actor/ActorActionTypes.h"
#include "jamnet/runtime/world/actor/ActorArchetypeDatabase.h"
#include "jamnet/runtime/world/simulation/client/ClientWorld.h"
#include "jamnet/runtime/world/simulation/common/CharacterControlTypes.h"
#include "jamnet/runtime/world/data/SharedDataManifest.h"
#include "jamnet/runtime/content/social/SocialTypes.h"
#include "jamnet/runtime/content/generic/GenericContentTypes.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>




namespace jam::net
{
	class ClientService;
	class ClientTcpSession;
	class ClientUdpSession;

	struct ClientConfig
	{
		AccountId			accountId			= kInvalidAccountId;
		uint32				authScheme			= 0;
		std::vector<uint8>	authField0;
		std::vector<uint8>	authField1;
		NetAddress			serverTcpAddress    = { "127.0.0.1", 7777 };
		NetAddress			serverUdpAddress    = { "127.0.0.1", 8888 };

		std::string			sharedDataManifestPath;
		bool				headlessMode		= false;
	};

	class ClientNetworkManager : public std::enable_shared_from_this<ClientNetworkManager>
	{
		friend class ClientTcpSession;
		friend class ClientUdpSession;


	public:
		explicit ClientNetworkManager(const ClientConfig& config);
		~ClientNetworkManager();

		// Frontend admission only. Work is serialized on the principal shard.
		bool                                    Connect();
		void                                    Disconnect();
		void                                    Close();

		// Return values report command admission, not execution success.
		bool									RequestWorldAction(const WorldActionCommand& command);
		bool									RequestActorAction(const ActorActionCommand& command);
		bool									RequestSocialCommand(const SocialCommand& command);
		bool									RequestGenericContent(const GenericContentRequest& request);

		void									SubmitCharacterControl(const CharacterControlIntent& intent);

		uint64									GetClientInstanceId() const { return m_clientInstanceId; }

	protected:
		bool                                    StartClientService();
		void                                    StopClientService(std::function<void()> completed = {});

		bool                                    ConnectTcp();
		bool                                    ConnectUdp();

		void                                    NotifyTcpBound(AccountId accountId, UserId userId);
		void                                    NotifyUdpBound(UserId userId);
		void                                    NotifyBootstrap(UserId userId, eBootstrapKind kind);
		void                                    NotifyTcpDisconnected(const ClientTcpSession* session);
		void                                    NotifyUdpDisconnected(const ClientUdpSession* session);

		void									PrepareMainWorld(const ClientWorldPrepare& prepare, std::function<void(bool)> completed);
		bool									CommitMainWorld(const ClientWorldCommit& commit);
		bool									ApplyMainWorldChanged(const UserWorldState& state);

	private:
		bool									ConnectOnPrincipalShard();
		void									DisconnectOnPrincipalShard(std::function<void()> completed = {});

		bool									RequestWorldActionOnPrincipalShard(const WorldActionCommand& command);
		bool									RequestActorActionOnPrincipalShard(const ActorActionCommand& command);
		bool									RequestSocialCommandOnPrincipalShard(const SocialCommand& command);
		bool									RequestGenericContentOnPrincipalShard(const GenericContentRequest& request);
		
		void									DrainCharacterControlOnPrincipalShard();
		void									ClearPendingCharacterControl();

		void									PublishActorActionFailure(ClientRequestId requestId, eActorAction action, eActorActionReason reason) const;
		void									ClosePrincipalAndWait();
		void									CompletePreparedMainWorld(ClientWorldPrepare prepare, ClientWorldBinding binding, std::function<void(bool)> completed);

		bool									DispatchWorldPacket(UserId userId, WorldId worldId, Packet packet);
		void									PublishSocialMessage(SocialMessage message) const;
		void									PublishGenericContentResponse(GenericContentResponse response) const;

		bool                                    IsConnected()	 const;
		bool                                    IsTcpConnected() const;
		bool                                    IsUdpConnected() const;

		AccountId								GetAccountId()		const { return m_principal.accountId; }
		UserId                                  GetUserId()			const { return m_principal.userId; }
		ClientMainWorldState&					GetMainWorldState()		  { return m_principal.mainWorld; }
		const ClientMainWorldState&				GetMainWorldState() const { return m_principal.mainWorld; }

		bool									IsOnPrincipalShard() const;
		void									AssertPrincipalAffinity() const;

		void									PublishNetworkStateEvent() const;
		void                                    UpdateSessionReadyState();

	private:
		ClientConfig                            m_config					= {};
		uint64									m_clientInstanceId			= 0;
		SharedDataManifest						m_manifest					= {};

		ClientPrincipalState					m_principal					= {};
		std::shared_ptr<ShardExecutor>			m_principalShard			= nullptr;

		std::shared_ptr<ClientService>          m_service					= nullptr;

		WorldTemplateDatabase					m_worldTemplates			= {};
		WorldArchetypeDatabase					m_worldArchetypes			= {};
		ActorArchetypeDatabase					m_actorArchetypes			= {};
		std::unique_ptr<WorldConfigResolver>	m_worldConfigResolver		= nullptr;

		std::atomic_bool                        m_running					= false;

		std::atomic_bool                        m_tcpBound					= false;
		std::atomic_bool                        m_udpBound					= false;
		std::atomic_bool                        m_sessionReady				= false;
		std::atomic<eBootstrapKind>             m_bootstrapKind				= eBootstrapKind::Pending;

		std::mutex								m_characterControlMutex;
		std::optional<CharacterControlIntent>	m_pendingCharacterControl;
		bool									m_characterControlDrainPending = false;
		uint32									m_characterControlRevision = 0;

	};
}
