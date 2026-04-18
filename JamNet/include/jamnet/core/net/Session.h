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
}

namespace jam::net
{
	enum class eServiceType : uint8;

	class Service;
	class PacketBuilder;


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


	class Session : public IocpObject
	{
		friend class UdpRouter;

	public:
		Session();
		virtual ~Session() override = default;

		virtual void							Init();

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

		bool									IsConnected() { return m_state.load(std::memory_order_relaxed) == eSessionState::CONNECTED; }

		Service*								GetService() const { return m_service; }
		void									SetService(Service* service);

		NetAddress&								GetRemoteNetAddress() { return m_remoteAddress; }
		void									SetRemoteNetAddress(const NetAddress& address) { m_remoteAddress = address; }
		SOCKET									GetSocket() const { return m_socket; }	// TCP has socket but UDP doesn't

		std::shared_ptr<Session>				Self() { return static_pointer_cast<Session>(shared_from_this()); }

		void									Post(Job j) const;
		void									Submit(Job j) const;
		void									SubmitAfter(Job j, uint64 delay_ns) const;

		entt::entity							GetEntity() const noexcept { return m_entity; }

		void									SetReady(bool ready) { m_isReady = ready; }
		bool									IsReady() { return m_isReady; }

	protected:
		virtual void							OnConnected() {}
		virtual void							OnDisconnected() {}
		virtual void							OnSend(int32 len) {}
		virtual void							OnRecv(BYTE* buffer, int32 len) {}

	private:
		void									EnsureBound();
		void									CreateEntity();


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
