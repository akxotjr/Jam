#pragma once
#include "IocpCore.h"
#include "Job.h"
#include "SessionEndpoint.h"


namespace jam::net
{
	class Service;
	class NetAddress;
	class RpcManager;
	class PacketBuilder;

				

	// Session ID
	using SessionId = uint16;
	// Mask & Shift
	constexpr uint16	PROTOCOL_MASK = 0x8000;			// 1000 0000 0000 0000
	constexpr uint16	ID_MASK = 0x7FFF;				// 0111 1111 1111 1111

	constexpr int32		PROTOCOL_SHIFT = 15;


	inline uint16 GenerateSID(eProtocolType protocol)
	{
		static Atomic<uint16> idGenerator = 1;
		return (static_cast<uint16>(protocol) << PROTOCOL_SHIFT) | (idGenerator.fetch_add(1) & ID_MASK);
	}

	inline eProtocolType ExtractProtocol(SessionId sid)
	{
		return static_cast<eProtocolType>((sid & PROTOCOL_MASK) >> PROTOCOL_SHIFT);
	}

	inline uint32 ExtractId(SessionId sid)
	{
		return sid & ID_MASK;
	}



	//enum class eSessionState : uint8
	//{
	//	CONNECTED		= 0,
	//	DISCONNECTED	= 1,
	//	HANDSHAKING		= 2
	//};




	class Session : public IocpObject
	{
		friend class UdpRouter;

	public:
		Session();
		virtual ~Session() = default;

		virtual bool							Connect() = 0;
		virtual void							Disconnect(const WCHAR* cause) = 0;
		virtual void							Send(const Sptr<SendBuffer>& sendBuffer) = 0;

		virtual void							Update() = 0;

		bool									IsTcp() { return ExtractProtocol(m_sid) == eProtocolType::TCP; }
		bool									IsUdp() { return ExtractProtocol(m_sid) == eProtocolType::UDP; }
		//bool									IsConnected() { return m_state == eSessionState::CONNECTED; }

		Sptr<Service>							GetService() { return m_service.lock(); }
		void									SetService(const Sptr<Service>& service) { m_service = service; }

		NetAddress&								GetRemoteNetAddress() { return m_remoteAddress; }
		void									SetRemoteNetAddress(const NetAddress& address) { m_remoteAddress = address; }
		SOCKET									GetSocket() { return m_socket; }

		Sptr<Session>							GetSession() { return static_pointer_cast<Session>(shared_from_this()); }
//		eSessionState							GetState() { return m_state; }

		void									AttachEndpoint(utils::exec::ShardDirectory& dir, utils::exec::RouteKey key);
		void									RebindRouteKey(utils::exec::RouteKey newKey);

		void									Post(utils::job::Job job);
		void									PostCtrl(utils::job::Job job);
		void									PostAfter(uint64 delay_ns, utils::job::Job j);

		void									JoinGroup(uint64 group_id, utils::exec::GroupHomeKey gk);
		void									LeaveGroup(uint64 group_id, utils::exec::GroupHomeKey gk);
		void									PostGroup(uint64 group_id, utils::exec::GroupHomeKey gk, utils::job::Job j);


	protected:
		// application level callback
		virtual void							OnConnected() = 0;
		virtual void							OnDisconnected() = 0;
		virtual void							OnSend(int32 len) = 0;
		virtual void							OnRecv(BYTE* buffer, int32 len) = 0;

	protected:   
		SOCKET									m_socket = INVALID_SOCKET;
		Wptr<Service>							m_service;
		NetAddress								m_remoteAddress = {};

		SessionId								m_sid;
		
		//Uptr<RpcManager>						m_rpcManager;

		Uptr<SessionEndpoint>					m_endpoint;
	};
}
