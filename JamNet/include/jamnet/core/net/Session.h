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

	inline const RouteDomain kTcpSessionRouteDomain = RouteDomain::From("TcpEndpoint");
	inline const RouteDomain kUdpSessionRouteDomain = RouteDomain::From("UdpEndpoint");

	using EndpointId = uint64;
	inline constexpr EndpointId kInvalidEndpointId = 0;

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
		friend class AdmissionContext;
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

		virtual void					HandleCustomPacket(Packet packet) {}

		virtual void                    OnSessionEstablished() {}
		virtual void                    OnSessionReleased() {}

		virtual bool					IsServerSide()		const { return false; }
		virtual bool					IsClientSide()		const { return false; }
		bool							IsTcp()				const { return m_protocol == eProtocolType::TCP; }
		bool							IsUdp()				const { return m_protocol == eProtocolType::UDP; }
		eProtocolType					GetProtocolType()	const { return m_protocol; }

		EndpointId						GetEndpointId()		const { return m_endpointId; }
		SessionId						GetSessionId()		const { return m_sessionId; }
		RouteKey						GetRouteKey()		const { return m_key; }
		void							SetRouteKey(RouteKey key) { m_key = key; }

		bool							IsConnected() const { return m_connected.load(std::memory_order_relaxed); }

		Service*						GetService() const { return m_service; }
		std::shared_ptr<Service>		GetServiceRef() const;
		void							SetService(Service* service) { m_service = service; }

		NetAddress						GetRemoteNetAddress() { return m_remoteAddress; }
		void							SetRemoteNetAddress(const NetAddress& address) { m_remoteAddress = address; }

		SOCKET							GetSocket() const { return m_socket; }	
		void							SetSocket(SOCKET socket);
		bool							MatchesEndpoint(EndpointId endpointId) const { return endpointId != kInvalidEndpointId && m_endpointId == endpointId; }

		void							Post(Job j) const;
		void							Submit(Job j) const;
		void							SubmitAfter(Job j, uint64 delay_ns) const;
		bool							IsCurrentShardContext() const;
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

		void							CompleteClose();
		void							CompleteProtocolDisconnect();

		static EndpointId				MakeEndpointId(const NetAddress& addr);
		static RouteKey					MakeTcpRouteKey(const NetAddress& remoteAddr);
		static RouteKey					MakeUdpRouteKey(const NetAddress& remoteAddr);

	protected:
		virtual void					OnSessionPrincipalUpdated() {}

		void							CompleteSessionEstablishment(bool ready);
		bool							AdoptAuthoritativeSessionId(SessionId sessionId);
	protected:   
		SOCKET							m_socket		= INVALID_SOCKET;

		Service*						m_service		= nullptr;	
		NetAddress						m_remoteAddress = {};
		eProtocolType					m_protocol		= eProtocolType::NONE;

		EndpointId						m_endpointId	= kInvalidEndpointId;
		uint64							m_accountId		= 0;
		RuntimeId						m_userId		= kInvalidRuntimeId;
		SessionId						m_sessionId		= kInvalidSessionId;

		std::atomic<bool>				m_connected		= false;

		bool							m_isReady		= false;
		bool							m_sessionEstablished = false;
		std::atomic<bool>				m_releaseQueued = false;
		std::atomic<bool>				m_protocolDisconnectCompleted = false;

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
