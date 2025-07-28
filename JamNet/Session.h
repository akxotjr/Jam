#pragma once
#include "IocpCore.h"

namespace jam::net
{
	class Service;
	class NetAddress;

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

	enum class eSessionState : uint8
	{
		CONNECTED = 0,
		DISCONNECTED,
		HANDSHAKING
	};





	//-----------------------------------------------------------------------------------------------//


	enum class ePacketType : uint8
	{
		SYSTEM = 0,
		RPC = 1,
		ACK = 2,
		CUSTOM = 3
	};
	
#pragma pack(push, 1)
	struct PacketHeader
	{
		uint16	size;
		uint8	type;
	};
#pragma pack(pop)

	constexpr uint8 RESPONSE_MASK = 0x00;
	constexpr uint8 RELIABLE_MASK = 0x01;
	constexpr uint8 COMPRESSED_MASK = 0x02;
	// ....

	enum class eRpcHeaderFlag : uint8
	{
		RESPONSE = 0,
		RELIABLE = 1,
		COMPRESSED = 2,

	};

#pragma pack(push, 1)
	struct RpcHeader
	{
		uint16	rpcId;
		uint32	requestid;
		uint8	flags;
	};
#pragma pack(pop)






	class Session : public IocpObject
	{
	public:
		Session() = default;
		virtual ~Session() = default;

		virtual bool							Connect() = 0;
		virtual void							Disconnect(const WCHAR* cause) = 0;
		virtual void							Send(const Sptr<SendBuffer>& sendBuffer) = 0;

		bool									IsTcp() const { return ExtractProtocol(m_sid) == eProtocolType::TCP; }
		bool									IsUdp() const { return ExtractProtocol(m_sid) == eProtocolType::UDP; }
		bool									IsConnected() const { return m_state == eSessionState::CONNECTED; }

		Sptr<Service>							GetService() const { return m_service.lock(); }
		void									SetService(const Sptr<Service>& service) { m_service = service; }

		NetAddress&								GetRemoteNetAddress() { return m_remoteAddress; }
		void									SetRemoteNetAddress(const NetAddress& address) { m_remoteAddress = address; }
		SOCKET									GetSocket() const { return m_socket; }

		Sptr<Session>							GetSession() { return static_pointer_cast<Session>(shared_from_this()); }
		eSessionState							GetState() const { return m_state; }

	protected:
		virtual void							OnConnected() = 0;
		virtual void							OnDisconnected() = 0;
		virtual void							OnSend(int32 len) = 0;
		virtual void							OnRecv(BYTE* buffer, int32 len) = 0;

	protected:
		SOCKET									m_socket = INVALID_SOCKET;
		Wptr<Service>							m_service;
		NetAddress								m_remoteAddress = {};

		SessionId								m_sid;
		eSessionState							m_state = eSessionState::DISCONNECTED; 
	};
}
