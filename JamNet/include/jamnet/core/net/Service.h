#pragma once
#include "jamnet/core/net/IocpCore.h"
#include "jamnet/core/net/NetAddress.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/TcpListener.h"
#include "jamnet/core/net/UdpRouter.h"


namespace jam::net
{
	enum class eServiceType : uint8
	{
		NONE,

		CLIENT,
		SERVER
	};

	struct ServiceConfig
	{
		NetAddress							localTcpAddress    = {};
		NetAddress							localUdpAddress    = {};
		NetAddress							remoteTcpAddress   = {};
		NetAddress							remoteUdpAddress   = {};
		uint32								maxTcpSessionCount = 1;
		uint32								maxUdpSessionCount = 1;
	};

	class Service
	{
		using SessionFactory = std::function<std::shared_ptr<Session>()>;
		using SessionInitCallback = std::function<void(std::shared_ptr<Session>)>;

		friend class TcpSession;
		friend class UdpSession;
		friend class TcpListener;
		friend class UdpRouter;

	public:
		Service(const ServiceConfig& config);
		virtual ~Service();


		void								Init();
		virtual bool						Start() = 0;

		bool								HasTcpFactory() const { return static_cast<bool>(m_tcpSessionFactory); }
		bool								HasUdpFactory() const { return static_cast<bool>(m_udpSessionFactory); }
		bool								CanStart() const { return m_tcpSessionFactory != nullptr || m_udpSessionFactory; }

		virtual void						CloseService();

		template<typename TCP, typename UDP>
		bool								SetSessionFactory();
		void								SetSessionInitCallback(const SessionInitCallback& callback) { m_sessionInitCallback = callback; }

		std::shared_ptr<Session>			CreateSession(eProtocolType protocol);

		void								RegisterTcpSession(const std::shared_ptr<TcpSession>& session);
		void								ReleaseTcpSession(const std::shared_ptr<TcpSession>& session);

		void								RegisterUdpSession(const std::shared_ptr<UdpSession>& session);
		void								ReleaseUdpSession(const std::shared_ptr<UdpSession>& session);

		void								RegisterHandshakingUdpSession(const std::shared_ptr<UdpSession>& session);
		void								ReleaseHandshakingUdpSession(const std::shared_ptr<UdpSession>& session);
		void								CompleteUdpHandshake(const NetAddress& from);
		std::shared_ptr<UdpSession>			FindSessionInConnected(const NetAddress& from);
		std::shared_ptr<UdpSession>			FindSessionInHandshaking(const NetAddress& from);

		void								ProcessUdpSession(const NetAddress& from, int32 numOfBytes, RecvBuffer& recvBuffer, uint64 ingressRecvTime_ns);

		int32								GetCurrentTcpSessionCount() const { return m_tcpSessionCount; }
		int32								GetMaxTcpSessionCount() const { return m_config.maxTcpSessionCount; }
		int32								GetCurrentUdpSessionCount() const { return m_udpSessionCount; }
		int32								GetMaxUdpSessionCount() const { return m_config.maxUdpSessionCount; }

		const NetAddress&					GetLocalTcpNetAddress() const { return m_config.localTcpAddress; }
		const NetAddress&					GetLocalUdpNetAddress() const { return m_config.localUdpAddress; }
		const NetAddress&					GetRemoteTcpNetAddress() const { return m_config.remoteTcpAddress; }
		const NetAddress&					GetRemoteUdpNetAddress() const { return m_config.remoteUdpAddress; }

		void								SetLocalTcpNetAddress(const NetAddress& addr) { m_config.localTcpAddress = addr; }
		void								SetLocalUdpNetAddress(const NetAddress& addr) { m_config.localUdpAddress = addr; }
		void								SetRemoteTcpNetAddress(const NetAddress& addr) { m_config.remoteTcpAddress = addr; }
		void								SetRemoteUdpNetAddress(const NetAddress& addr) { m_config.remoteUdpAddress = addr; }

		eServiceType						GetServiceType() const { return m_type; }

		void*								GetUserData() { return m_userData; }
		void								SetUserData(void* userData) { m_userData = userData; }

	private:
		IocpCore*							GetIocpCore() const { return m_iocpCore.get(); }


	protected:
		USE_LOCK

		ServiceConfig										m_config;
		eServiceType										m_type = eServiceType::NONE;
		std::shared_ptr<IocpCore>							m_iocpCore;

		std::unordered_map<NetAddress, std::shared_ptr<TcpSession>>	m_tcpSessions;
		std::unordered_map<NetAddress, std::shared_ptr<UdpSession>>	m_udpSessions;
		std::unordered_map<NetAddress, std::shared_ptr<UdpSession>>	m_handshakingUdpSessions;


		int32												m_sessionCount = 0;
		int32												m_tcpSessionCount = 0;
		int32												m_udpSessionCount = 0;

		SessionFactory										m_tcpSessionFactory;
		SessionFactory										m_udpSessionFactory;
		SessionInitCallback									m_sessionInitCallback;

		std::shared_ptr<TcpListener>						m_listener = nullptr;
		std::shared_ptr<UdpRouter>							m_udpRouter = nullptr;

		std::atomic<bool>									m_running{ false };
		uint64												m_lastUpdateTime_ns = 0_ns;

		void*												m_userData = nullptr;
	};



	template<typename TCP, typename UDP>
	inline bool Service::SetSessionFactory()
	{
		if (!std::is_base_of_v<Session, TCP> || !std::is_base_of_v<Session, UDP>)
			return false;

		m_tcpSessionFactory = []() -> std::shared_ptr<Session>
			{
				return MakeShared<TCP>();
			};

		m_udpSessionFactory = []() -> std::shared_ptr<Session>
			{
				return MakeShared<UDP>();
			};
		return true;
	}




	class ClientService : public Service
	{
	public:
		ClientService(const ServiceConfig& config);
		virtual ~ClientService() override = default;

		bool Start() override;
	};



	class ServerService : public Service
	{
	public:
		ServerService(const ServiceConfig& config);
		virtual ~ServerService() override = default;

		bool Start() override;
	};


}

