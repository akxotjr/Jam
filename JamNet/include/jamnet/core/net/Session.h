#pragma once
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

	enum class eSessionState : uint8
	{
		CONNECTED,
		DISCONNECTED,
	};

	inline const RouteDomain kTcpSessionRouteDomain = RouteDomain::From("TcpEndpoint");
	inline const RouteDomain kUdpSessionRouteDomain = RouteDomain::From("UdpEndpoint");

	struct SessionHandle
	{
		RouteKey	routeKey	= {};
		uint64		sessionId	= 0;

		bool operator==(const SessionHandle& other) const
		{
			return routeKey == other.routeKey && sessionId == other.sessionId;
		}

		bool IsValid() const
		{
			return IsValidRouteKey(routeKey) && sessionId != 0;
		}
	};

	class Session;
	Session*		FindSessionByHandle(ShardLocal& L, const SessionHandle& handle);
	const Session*	FindSessionByHandle(const ShardLocal& L, const SessionHandle& handle);

	class Session : public IocpObject
	{
		friend class Service;
		friend class UdpRouter;

	public:
		Session();
		virtual ~Session() override = default;

		virtual void							Init(const NetAddress& remoteAddr);

		virtual bool							Connect() = 0;
		virtual void							Disconnect() = 0;
		virtual void							Send(Packet packet) = 0;

		virtual void							HandleCustomPacket(const PacketHeaderView& view) {}

		virtual void                            OnLinkEstablished() {}
		virtual void                            OnLinkTerminated() {}

		bool									IsTcp() const { return m_protocol == eProtocolType::TCP; }
		bool									IsUdp() const { return m_protocol == eProtocolType::UDP; }
		eProtocolType							GetProtocolType() const { return m_protocol; }

		bool									IsServerSide() const;
		bool									IsClientSide() const;

		uint64									GetSessionId() const { return m_sessionId; }
		RouteKey								GetRouteKey() const { return m_key; }
		SessionHandle							GetSessionHandle() const { return SessionHandle{ m_key, m_sessionId }; }
		void									SetRouteKey(RouteKey key) { m_key = key; }

		bool									IsConnected() { return m_state.load(std::memory_order_relaxed) == eSessionState::CONNECTED; }

		Service*								GetService() const { return m_service; }
		void									SetService(Service* service);

		NetAddress								GetRemoteNetAddress() { return m_remoteAddress; }
		void									SetRemoteNetAddress(const NetAddress& address) { m_remoteAddress = address; }

		SOCKET									GetSocket() const { return m_socket; }	
		void									SetSocket(SOCKET socket);
		bool									MatchesSessionHandle(const SessionHandle& handle) const;

		void									Post(Job j) const;
		void									Submit(Job j) const;
		void									SubmitAfter(Job j, uint64 delay_ns) const;

		entt::entity							GetEntity() const noexcept { return m_entity; }
		void									CreateEntity();

		void									SetReady(bool ready) { m_isReady = ready; }
		bool									IsReady() { return m_isReady; }


		static uint64							MakeEndpointId(const NetAddress& addr);
		static RouteKey							MakeTcpRouteKey(const NetAddress& remoteAddr);
		static RouteKey							MakeUdpRouteKey(const NetAddress& remoteAddr);

	protected:
		virtual void							OnConnected() {}
		virtual void							OnDisconnected() {}
		virtual void							OnSend(int32 len) {}
		virtual void							OnRecv(BYTE* buffer, int32 len) {}
		virtual void							OnEntityCreated(entt::registry& R, entt::entity e) {}

	private:
		void									EnsureBound();


	protected:   
		SOCKET									m_socket		= INVALID_SOCKET;

		Service*								m_service		= nullptr;	
		NetAddress								m_remoteAddress = {};
		eProtocolType							m_protocol		= eProtocolType::NONE;

		static std::atomic<uint64>				s_sessionIdGenerator;
		uint64									m_sessionId;

		std::atomic<eSessionState>				m_state{ eSessionState::DISCONNECTED };

		bool									m_isReady = false;

	private:
		RouteKey								m_key				= {};
		std::weak_ptr<ShardExecutor>			m_boundShard;
		std::shared_ptr<Mailbox>				m_mailbox			= nullptr;

		entt::entity							m_entity			= entt::null;

		std::atomic<bool>						m_closed			= false;
		std::atomic<bool>						m_entityReady		= false;
		std::atomic<bool>						m_entityCreating	= false;
		std::atomic<bool>						m_pendingConnect	= false;
	};
}

template <>
struct std::hash<jam::net::SessionHandle>
{
	size_t operator()(const jam::net::SessionHandle& handle) const noexcept
	{
		const size_t routeHash = std::hash<uint64>()(handle.routeKey.value());
		const size_t idHash = std::hash<uint64>()(handle.sessionId);
		return routeHash ^ (idHash + 0x9e3779b97f4a7c15ull + (routeHash << 6) + (routeHash >> 2));
	}
};
