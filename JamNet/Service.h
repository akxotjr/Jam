#pragma once

namespace jam::net
{
	using SessionFactory = std::function<SessionRef()>;

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
		Service(TransportConfig config, IocpCoreRef core, int32 maxSTcpSessionCount = 1, int32 maxUdpSessionCount = 1);
		virtual ~Service();

		virtual bool						Start();
		bool								CanStart() const { return _tcpSessionFactory != nullptr || _udpSessionFactory; }

		virtual void						CloseService();

		template<typename TCP, typename UDP>
		void								SetSessionFactory();


		void								Broadcast(SendBufferRef sendBuffer);
		SessionRef							CreateSession(ProtocolType protocol);

		void								AddTcpSession(TcpSessionRef session);
		void								ReleaseTcpSession(TcpSessionRef session);

		void								AddUdpSession(ReliableUdpSessionRef session);
		void								ReleaseUdpSession(ReliableUdpSessionRef session);


		int32								GetCurrentTcpSessionCount() const { return _tcpSessionCount; }
		int32								GetMaxTcpSessionCount() const { return _maxTcpSessionCount; }
		int32								GetCurrentUdpSessionCount() const { return _udpSessionCount; }
		int32								GetMaxUdpSessionCount() const { return _maxUdpSessionCount; }


		void								SetUdpReceiver(UdpReceiverRef udpReceiver) { _udpReceiver = udpReceiver; };
		SOCKET								GetUdpSocket() const { return _udpReceiver->GetSocket(); }

		ReliableUdpSessionRef				FindOrCreateUdpSession(const NetAddress& from);
		void								CompleteUdpHandshake(const NetAddress& from);

	public:
		NetAddress							GetTcpNetAddress() const { return _config.tcpAddress.value_or(NetAddress(L"0.0.0.0", 0)); }
		NetAddress							GetUdpNetAddress() const { return _config.udpAddress.value_or(NetAddress(L"0.0.0.0", 0)); }
		IocpCoreRef& GetIocpCore() { return _iocpCore; }

	protected:
		USE_LOCK

			TransportConfig										_config;

		IocpCoreRef											_iocpCore;

		Set<TcpSessionRef>									_tcpSessions;
		Set<ReliableUdpSessionRef>							_udpSessions;
		unordered_map<NetAddress, ReliableUdpSessionRef>	_pendingUdpSessions;


		int32												_sessionCount = 0;
		int32												_maxSessionCount = 0;

		int32												_tcpSessionCount = 0;
		int32												_maxTcpSessionCount = 0;
		int32												_udpSessionCount = 0;
		int32												_maxUdpSessionCount = 0;

		SessionFactory										_tcpSessionFactory;
		SessionFactory										_udpSessionFactory;

	private:
		ListenerRef											_listener = nullptr;
		UdpReceiverRef										_udpReceiver = nullptr;
	};

	template<typename TCP, typename UDP>
	inline void Service::SetSessionFactory()
	{
		_tcpSessionFactory = []() -> SessionRef
			{
				return memory::MakeShared<TCP>();
			};

		_udpSessionFactory = []() -> SessionRef
			{
				return memory::MakeShared<UDP>();
			};
	}
}

