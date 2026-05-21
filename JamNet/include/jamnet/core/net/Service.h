#pragma once

#include "jamnet/core/executor/Lock.h"
#include "jamnet/core/executor/ShardExecutor.h"

#include "jamnet/core/utils/TimeUnits.h"

#include "jamnet/core/net/IocpCore.h"
#include "jamnet/core/net/NetAddress.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/SessionShardState.h"
#include "jamnet/core/net/TcpListener.h"
#include "jamnet/core/net/UdpRouter.h"


namespace jam::net
{
	class TcpSession;
	class UdpSession;

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

		using TcpFactory = std::function<std::unique_ptr<TcpSession>()>;
		using UdpFactory = std::function<std::unique_ptr<UdpSession>()>;

		using SessionInitCallback = std::function<void(Session*)>;

		friend class TcpSession;
		friend class UdpSession;
		friend class Session;
		friend class TcpListener;
		friend class UdpRouter;

	public:
		Service(const ServiceConfig& config);
		virtual ~Service();


		void								Init();
		virtual bool						Start() = 0;

		bool								HasTcpFactory() const { return static_cast<bool>(m_tcpFactory); }
		bool								HasUdpFactory() const { return static_cast<bool>(m_udpFactory); }
		bool								CanStart()		const { return m_tcpFactory != nullptr || m_udpFactory; }

		virtual void						CloseService();

		template<typename TCP, typename UDP>
		bool								SetSessionFactory();
		void								SetSessionInitCallback(const SessionInitCallback& callback) { m_sessionInitCallback = callback; }

		std::unique_ptr<TcpSession>			CreateTcpSession(const NetAddress& remoteAddr);
		std::unique_ptr<UdpSession>			CreateUdpSession(const NetAddress& remoteAddr);
		std::unique_ptr<TcpSession>			CreateTcpSession();		// using internal remote tcp address
		std::unique_ptr<UdpSession>			CreateUdpSession();		// using internal remote udp address
		void								NotifyTcpSessionAttached();
		void								NotifyUdpSessionAttached(uint64 endpointId, RouteKey routeKey);

		void								ReleaseTcpSession(TcpSession* session);
		void								ReleaseUdpSession(UdpSession* session);

		int32								GetCurrentTcpSessionCount() const { return m_tcpSessionCount.load(std::memory_order_relaxed); }
		int32								GetMaxTcpSessionCount()		const { return m_config.maxTcpSessionCount; }
		int32								GetCurrentUdpSessionCount() const { return m_udpSessionCount.load(std::memory_order_relaxed); }
		int32								GetMaxUdpSessionCount()		const { return m_config.maxUdpSessionCount; }

		const NetAddress&					GetLocalTcpNetAddress()		const { return m_config.localTcpAddress; }
		const NetAddress&					GetLocalUdpNetAddress()		const { return m_config.localUdpAddress; }
		const NetAddress&					GetRemoteTcpNetAddress()	const { return m_config.remoteTcpAddress; }
		const NetAddress&					GetRemoteUdpNetAddress()	const { return m_config.remoteUdpAddress; }

		void								SetLocalTcpNetAddress(const NetAddress& addr)  { m_config.localTcpAddress  = addr; }
		void								SetLocalUdpNetAddress(const NetAddress& addr)  { m_config.localUdpAddress  = addr; }
		void								SetRemoteTcpNetAddress(const NetAddress& addr) { m_config.remoteTcpAddress = addr; }
		void								SetRemoteUdpNetAddress(const NetAddress& addr) { m_config.remoteUdpAddress = addr; }

		eServiceType						GetServiceType() const { return m_type; }

		void*								GetUserData() { return m_userData; }
		void								SetUserData(void* userData) { m_userData = userData; }

	private:
		IocpCore*							GetIocpCore() const { return m_iocpCore.get(); }
		bool								RegisterIocpObject(IocpObject* object);
		std::unique_ptr<TcpSession>			MakeTcpSession(const NetAddress& remoteAddr);
		std::unique_ptr<UdpSession>			MakeUdpSession(const NetAddress& remoteAddr);


	protected:
		USE_LOCK

		ServiceConfig										m_config;
		eServiceType										m_type					= eServiceType::NONE;
		std::shared_ptr<IocpCore>							m_iocpCore;


		std::atomic<int32>									m_sessionCount			= 0;
		std::atomic<int32>									m_tcpSessionCount		= 0;
		std::atomic<int32>									m_udpSessionCount		= 0;

		TcpFactory											m_tcpFactory;
		UdpFactory											m_udpFactory;
		SessionInitCallback									m_sessionInitCallback;

		std::shared_ptr<TcpListener>						m_listener				= nullptr;
		std::shared_ptr<UdpRouter>							m_udpRouter				= nullptr;

		std::atomic<bool>									m_running				= false;
		uint64												m_lastUpdateTime_ns		= 0_ns;

		void*												m_userData				= nullptr;
	};



	template<typename TCP, typename UDP>
	inline bool Service::SetSessionFactory()
	{
		if (!std::is_base_of_v<Session, TCP> || !std::is_base_of_v<Session, UDP>)
			return false;

		m_tcpFactory = []() -> std::unique_ptr<TcpSession>
			{
				return std::make_unique<TCP>();
			};

		m_udpFactory = []() -> std::unique_ptr<UdpSession>
			{
				return std::make_unique<UDP>();
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
