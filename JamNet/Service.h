#pragma once
#include "NetAddress.h"
#include "Session.h"
#include "TcpListener.h"
#include "UdpRouter.h"

namespace jam::net
{
	enum class eProtocolType : uint8
	{
		TCP,
		UDP
	};

	enum class ePeerType
	{
		Client,
		Server,

		None
	};

	struct TransportConfig
	{
		NetAddress localTcpAddress = {};
		NetAddress localUdpAddress = {};
		NetAddress remoteTcpAddress = {};
		NetAddress remoteUdpAddress = {};
	};

	class Service : public enable_shared_from_this<Service>
	{
		using SessionFactory = std::function<Sptr<Session>()>;

		friend class TcpSession;
		friend class UdpSession;
		friend class TcpListener;
		friend class UdpRouter;

	public:
		Service(TransportConfig config, int32 maxTcpSessionCount = 1, int32 maxUdpSessionCount = 1);
		virtual ~Service();

		virtual bool						Start() = 0;
		bool								CanStart() const { return m_tcpSessionFactory != nullptr || m_udpSessionFactory; }

		virtual void						CloseService();

		template<typename TCP, typename UDP>
		bool								SetSessionFactory();

		Sptr<Session>						CreateSession(eProtocolType protocol);

		void								AddTcpSession(Sptr<TcpSession> session);
		void								ReleaseTcpSession(Sptr<TcpSession> session);

		void								AddUdpSession(Sptr<UdpSession> session);
		void								ReleaseUdpSession(Sptr<UdpSession> session);

		void								AddHandshakingUdpSession(Sptr<UdpSession> session);


		int32								GetCurrentTcpSessionCount() const { return m_tcpSessionCount; }
		int32								GetMaxTcpSessionCount() const { return m_maxTcpSessionCount; }
		int32								GetCurrentUdpSessionCount() const { return m_udpSessionCount; }
		int32								GetMaxUdpSessionCount() const { return m_maxUdpSessionCount; }

		void								CompleteUdpHandshake(const NetAddress& from);

		Sptr<UdpSession>					FindSessionInConnected(const NetAddress& from);
		Sptr<UdpSession>					FindSessionInHandshaking(const NetAddress& from);

		Sptr<UdpSession>					CreateAndRegisterToHandshaking(const NetAddress& from);

		void								ProcessUdpSession(const NetAddress& from, int32 numOfBytes, RecvBuffer recvBuffer);

	public:
		const NetAddress&					GetLocalTcpNetAddress() const { return m_config.localTcpAddress; }
		const NetAddress&					GetLocalUdpNetAddress() const { return m_config.localUdpAddress; }
		const NetAddress&					GetRemoteTcpNetAddress() const { return m_config.remoteTcpAddress; }
		const NetAddress&					GetRemoteUdpNetAddress() const { return m_config.remoteUdpAddress; }

		void								SetLocalTcpNetAddress(const NetAddress& addr) { m_config.localTcpAddress = addr; }
		void								SetlocalUdpNetAddress(const NetAddress& addr) { m_config.localUdpAddress = addr; }
		void								SetRemoteTcpNetAddress(const NetAddress& addr) { m_config.remoteTcpAddress = addr; }
		void								SetRemoteUdpNetAddress(const NetAddress& addr) { m_config.remoteUdpAddress = addr; }

		IocpCore*							GetIocpCore() { return m_iocpCore.get(); }

	protected:
		USE_LOCK

		TransportConfig										m_config;

		Uptr<IocpCore>										m_iocpCore;

		xumap<NetAddress, Sptr<TcpSession>>					m_tcpSessions;
		xumap<NetAddress, Sptr<UdpSession>>					m_udpSessions;
		xumap<NetAddress, Sptr<UdpSession>>					m_handshakingUdpSessions;


		int32												m_sessionCount = 0;
		int32												m_maxSessionCount = 0;

		int32												m_tcpSessionCount = 0;
		int32												m_maxTcpSessionCount = 0;
		int32												m_udpSessionCount = 0;
		int32												m_maxUdpSessionCount = 0;

		SessionFactory										m_tcpSessionFactory;
		SessionFactory										m_udpSessionFactory;

		Sptr<TcpListener>									m_listener = nullptr;
		Sptr<UdpRouter>										m_udpRouter = nullptr;

		ePeerType											m_peer = ePeerType::None;
	};

	template<typename TCP, typename UDP>
	inline bool Service::SetSessionFactory()
	{
		if (!std::is_base_of_v<Session, TCP> || !std::is_base_of_v<Session, UDP>)
			return false;

		m_tcpSessionFactory = []() -> Sptr<Session>
			{
				return jam::utils::memory::MakeShared<TCP>();
			};

		m_udpSessionFactory = []() -> Sptr<Session>
			{
				return jam::utils::memory::MakeShared<UDP>();
			};
		return true;
	}




	class ClientService : public Service
	{
	public:
		ClientService(TransportConfig config, int32 maxTcpSessionCount = 1, int32 maxUdpSessionCount = 1);
		~ClientService() override;

		bool Start() override;
	};



	class ServerService : public Service
	{
	public:
		ServerService(TransportConfig config, int32 maxTcpSessionCount = 1, int32 maxUdpSessionCount = 1);
		~ServerService() override;

		bool Start() override;
	};


}

