#pragma once
#include "jamnet/core/executor/ShardOwnedObject.h"
#include "jamnet/core/executor/RuntimeId.h"
#include "jamnet/core/executor/ShardRoutingPolicy.h"
#include "jamnet/core/net/IocpCore.h"
#include "jamnet/core/net/NetAddress.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/SessionComponents.h"

namespace jam
{
	class Job;
	class ShardExecutor;
	class Mailbox;
	struct ShardLocal;
}

namespace jam::net
{
	enum class eServiceType : uint8;

	class Service;
	class PacketBuilder;
	class Session;

	enum class eProtocolType : uint8
	{
		TCP,
		UDP,

		NONE
	};

	// Transition summary:
	// Disconnected -> Connected -> Authenticating -> Binding -> Bound -> Ready
	// Connected: transport connected, Authenticating: principal verification in flight, Binding: bind in flight,
	// Bound    : sessionId issued and shard binding completed,
	// Ready    : entity created and link established callbacks are valid.
	enum class eSessionState : uint8
	{
		Disconnected,
		Connected,
		Authenticating,
		Binding,
		Bound,
		Ready,
	};

	inline const RouteDomain kTcpSessionRouteDomain = RouteDomain::From("TcpEndpoint");
	inline const RouteDomain kUdpSessionRouteDomain = RouteDomain::From("UdpEndpoint");

	struct EndpointHandle
	{
		RouteKey	routeKey	= {};
		uint64		endpointId	= 0;

		bool operator==(const EndpointHandle& other) const
		{
			return routeKey == other.routeKey && endpointId == other.endpointId;
		}

		bool IsValid() const
		{
			return IsValidRouteKey(routeKey) && endpointId != 0;
		}
	};

	template<typename T>
	using SessionRef = ShardOwnedObjectRefSlot<T>;

	template<typename TcpT, typename UdpT>
	struct SessionRefBundle
	{
		SessionRef<TcpT> tcp = {};
		SessionRef<UdpT> udp = {};

		TcpT*	TryGetTcp()		const { return tcp.TryGet(); }
		UdpT*	TryGetUdp()		const { return udp.TryGet(); }
		TcpT*	TryGetTcpRaw()	const { return tcp.TryGetRaw(); }
		UdpT*	TryGetUdpRaw()	const { return udp.TryGetRaw(); }

		bool	HasTcp()		const { return tcp.TryGet() != nullptr; }
		bool	HasUdp()		const { return udp.TryGet() != nullptr; }
	};



	class Session : public IocpObject, public IShardOwnedObject
	{
		friend class Service;
		friend class UdpRouter;

	protected:
		struct BindRetryState
		{
			bool	active		= false;
			bool	bound		= false;
			uint8	retryCount	= 0;
			uint32	timerToken	= 0;
		};

	public:
		Session() = default;
		virtual ~Session() override = default;

		virtual void					Init(const NetAddress& remoteAddr);

		virtual bool					Connect() = 0;
		virtual void					Disconnect() = 0;
		// Caller must enter the session's owning shard before sending.
		virtual void					Send(Packet packet) = 0;

		RuntimeId						GetShardOwnedRuntimeId()  const override { return m_sessionId; }
		MailboxRef						GetShardOwnedMailboxRef() const override { return m_mailboxRef; }
		bool							BeginClose(eMailboxCloseMode mode, std::function<void()> onClosed) override;

		virtual void					HandlePreBindSystemPacket(const PacketHeaderView& view) {}
		virtual void					HandleCustomPacket(Packet packet) {}

		virtual void                    OnLinkEstablished() {}
		virtual void                    OnLinkTerminated()  {}
		virtual bool                    CanNotifyLinkEstablished() const { return true; }

		virtual bool					IsServerSide()		const { return false; }
		virtual bool					IsClientSide()		const { return false; }
		bool							IsTcp()				const { return m_protocol == eProtocolType::TCP; }
		bool							IsUdp()				const { return m_protocol == eProtocolType::UDP; }
		eProtocolType					GetProtocolType()	const { return m_protocol; }

		uint64							GetEndpointId()		const { return m_endpointId; }
		SessionId						GetSessionId()		const { return m_sessionId; }
		uint32							GetServiceGeneration() const { return m_serviceGeneration; }
		RouteKey						GetRouteKey()		const { return m_key; }
		EndpointHandle					GetEndpointHandle() const { return EndpointHandle{ m_key, m_endpointId }; }
		void							SetRouteKey(RouteKey key) { m_key = key; }

		bool							IsConnected() { return m_state.load(std::memory_order_relaxed) != eSessionState::Disconnected; }

		Service*						GetService() const { return m_service; }
		std::shared_ptr<Service>		GetServiceRef() const;
		void							SetService(Service* service) { m_service = service; }

		NetAddress						GetRemoteNetAddress() { return m_remoteAddress; }
		void							SetRemoteNetAddress(const NetAddress& address) { m_remoteAddress = address; }

		SOCKET							GetSocket() const { return m_socket; }	
		void							SetSocket(SOCKET socket);
		bool							MatchesEndpointHandle(const EndpointHandle& handle) const;

		void							Post(Job j) const;
		void							Submit(Job j) const;
		void							SubmitAfter(Job j, uint64 delay_ns) const;
		bool							IsCurrentShardContext() const;
		void							Rehome(RouteKey newRouteKey, std::function<void(bool)> onDone = {});

		entt::entity					GetEntity() const noexcept { return m_entity; }
		void							CreateEntity();
		virtual bool					IsPreBindPhase() const { return m_sessionId == kInvalidSessionId; }
		virtual bool					CanCreateSessionEntity() const { return m_sessionId != kInvalidSessionId; }

		void							SetReady(bool ready) { m_isReady = ready; }
		bool							IsReady() { return m_isReady; }
		void							SetAccountId(uint64 accountId) { m_accountId = accountId; OnSessionPrincipalUpdated(); }
		uint64							GetAccountId() const { return m_accountId; }
		void							SetUserId(RuntimeId userId) { m_userId = userId; OnSessionPrincipalUpdated(); }
		RuntimeId						GetUserId() const { return m_userId; }

		void							FinalizeShardOwnedClose();
		void							CompleteProtocolDisconnect();

		static uint64					MakeEndpointId(const NetAddress& addr);
		static RouteKey					MakeTcpRouteKey(const NetAddress& remoteAddr);
		static RouteKey					MakeUdpRouteKey(const NetAddress& remoteAddr);

	protected:
		virtual void					OnDisconnected() {}
		virtual void					OnSessionPrincipalUpdated() {}

		void							IssueSessionId();
		bool							AdoptAuthoritativeSessionId(SessionId sessionId);

		bool							TryBeginServerBind();
		void							EndServerBind();
		void							SetSessionState(eSessionState state) { m_state.store(state, std::memory_order_relaxed); }

		void							NotifyLinkEstablishedIfReady();
		void							NotifyLinkTerminatedIfEstablished();
		void							NotifyDisconnectedOnce();

	protected:   
		SOCKET							m_socket		= INVALID_SOCKET;

		Service*						m_service		= nullptr;	
		NetAddress						m_remoteAddress = {};
		eProtocolType					m_protocol		= eProtocolType::NONE;

		uint64							m_endpointId	= 0;
		uint64							m_accountId		= 0;
		RuntimeId						m_userId		= kInvalidRuntimeId;
		SessionId						m_sessionId		= kInvalidSessionId;
		uint32							m_serviceGeneration = 0;

		std::atomic<eSessionState>		m_state			= eSessionState::Disconnected;

		bool							m_isReady		= false;
		std::atomic<bool>				m_releaseQueued = false;
		bool							m_linkEstablishedNotified = false;
		std::atomic<bool>				m_protocolDisconnectCompleted = false;
		std::atomic<bool>				m_disconnectedNotified = false;

		BindRetryState					m_clientBind	= {};

	private:
		RouteKey						m_key				= {};
		std::weak_ptr<ShardExecutor>	m_shard;
		MailboxRef						m_mailboxRef = {};

		entt::entity					m_entity			= entt::null;

		std::atomic<bool>				m_closed			= false;

		std::function<void()>			m_onShardOwnedClosed = {};

		friend class ClientService;
	};
}

template <>
struct std::hash<jam::net::EndpointHandle>
{
	size_t operator()(const jam::net::EndpointHandle& handle) const noexcept
	{
		const size_t routeHash = std::hash<uint64>()(handle.routeKey.value());
		const size_t idHash = std::hash<uint64>()(handle.endpointId);
		return routeHash ^ (idHash + 0x9e3779b97f4a7c15ull + (routeHash << 6) + (routeHash >> 2));
	}
};
