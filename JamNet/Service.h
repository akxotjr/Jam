#pragma once
#include "Session.h"
#include "Listener.h"
#include "UdpReceiver.h"

namespace jam::net
{
	enum class EProtocolType : uint8
	{
		TCP,
		UDP
	};

	class TcpSession;
	class UdpSession;


	using SessionFactory = std::function<Sptr<Session>()>;

	/*--------------
		 Service
	---------------*/

	struct TransportConfig
	{
		std::optional<NetAddress> tcpAddress;
		std::optional<NetAddress> udpAddress;
	};

	class Service : public enable_shared_from_this<Service>
	{
	public:
		Service(TransportConfig config, Sptr<IocpCore> core, int32 maxSTcpSessionCount = 1, int32 maxUdpSessionCount = 1);
		virtual ~Service();

		virtual bool						Start();
		bool								CanStart() const { return m_tcpSessionFactory != nullptr || m_udpSessionFactory; }

		virtual void						CloseService();

		template<typename TCP, typename UDP>
		void								SetSessionFactory();


		void								Broadcast(Sptr<SendBuffer> sendBuffer);
		Sptr<Session>						CreateSession(EProtocolType protocol);

		void								AddTcpSession(Sptr<TcpSession> session);
		void								ReleaseTcpSession(Sptr<TcpSession> session);

		void								AddUdpSession(Sptr<UdpSession> session);
		void								ReleaseUdpSession(Sptr<UdpSession> session);


		int32								GetCurrentTcpSessionCount() const { return m_tcpSessionCount; }
		int32								GetMaxTcpSessionCount() const { return m_maxTcpSessionCount; }
		int32								GetCurrentUdpSessionCount() const { return m_udpSessionCount; }
		int32								GetMaxUdpSessionCount() const { return m_maxUdpSessionCount; }


		void								SetUdpReceiver(Sptr<UdpReceiver> udpReceiver) { m_udpReceiver = udpReceiver; };
		SOCKET								GetUdpSocket() const { return m_udpReceiver->GetSocket(); }

		Sptr<UdpSession>					FindOrCreateUdpSession(const NetAddress& from);
		void								CompleteUdpHandshake(const NetAddress& from);

	public:
		NetAddress							GetTcpNetAddress() const { return m_config.tcpAddress.value_or(NetAddress(L"0.0.0.0", 0)); }
		NetAddress							GetUdpNetAddress() const { return m_config.udpAddress.value_or(NetAddress(L"0.0.0.0", 0)); }
		Sptr<IocpCore>&						GetIocpCore() { return m_iocpCore; }

	protected:
		USE_LOCK

		TransportConfig										m_config;

		Sptr<IocpCore>										m_iocpCore;

		xset<Sptr<TcpSession>>								m_tcpSessions;
		xset<Sptr<UdpSession>>								m_udpSessions;
		unordered_map<NetAddress, Sptr<UdpSession>>			m_pendingUdpSessions;


		int32												m_sessionCount = 0;
		int32												m_maxSessionCount = 0;

		int32												m_tcpSessionCount = 0;
		int32												m_maxTcpSessionCount = 0;
		int32												m_udpSessionCount = 0;
		int32												m_maxUdpSessionCount = 0;

		SessionFactory										m_tcpSessionFactory;
		SessionFactory										m_udpSessionFactory;

	private:
		Sptr<TcpListener>										m_listener = nullptr;
		Sptr<UdpReceiver>									m_udpReceiver = nullptr;
	};

	template<typename TCP, typename UDP>
	inline void Service::SetSessionFactory()
	{
		m_tcpSessionFactory = []() -> Sptr<Session>
			{
				return jam::utils::memory::MakeShared<TCP>();
			};

		m_udpSessionFactory = []() -> Sptr<Session>
			{
				return jam::utils::memory::MakeShared<UDP>();
			};
	}
}

