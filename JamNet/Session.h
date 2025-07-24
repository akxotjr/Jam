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
		CONNECTED		= 0,
		DISCONNECTED	= 1,
		HANDSHAKING		= 2
	};


	//----------------------------------------------------------------------------------//


#pragma pack(push, 1)
	struct PacketHeader
	{
		uint16 sizeAndflags;    // [4-bit flags][12-bit size]   total packet size and flags
		uint8  type;			// packet type (System, RPC, Ack, Custom)
	};
#pragma pack(pop)

	enum class ePacketType : uint8
	{
		SYSTEM		= 0,
		RPC			= 1,
		ACK			= 2,
		CUSTOM		= 3
	};

	// mask & shift
	constexpr uint16 PACKET_FLAG_MASK = 0xF000;
	constexpr uint16 PACKET_SIZE_MASK = 0x0FFF;
	constexpr uint16 PACKET_FLAG_SHIFT = 12;

	// flags
	constexpr uint16 FLAG_HAS_RUDP = 0x1000;
	constexpr uint16 FLAG_IS_COMPRESSED = 0x2000;
	constexpr uint16 FLAG_IS_ENCRYPTED = 0x3000;

	inline uint16 GetPacketSize(uint16 sizeAndFlags)
	{
		return sizeAndFlags & PACKET_SIZE_MASK;
	}

	inline uint8 GetPacketFlags(uint16 sizeAndFlags)
	{
		return static_cast<uint8>((sizeAndFlags & PACKET_FLAG_MASK) >> PACKET_FLAG_SHIFT);
	}

	inline uint16 MakeSizeAndFlags(uint16 size, uint8 flags)
	{
		return ((flags & 0x0F) << PACKET_FLAG_SHIFT) | (size & PACKET_SIZE_MASK);
	}

	//----------------------------------------------------------------------------------//




	//----------------------------------------------------------------------------------//


#pragma pack(push, 1)
	struct RpcHeader
	{
		uint16 rpcId;        // 어떤 RPC인지
		uint32 requestId;    // 응답 매칭용
		uint8  flags;        // 예: isResponse, isReliable, isCompressed 등
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

		bool									IsTcp() { return ExtractProtocol(m_sid) == eProtocolType::TCP; }
		bool									IsUdp() { return ExtractProtocol(m_sid) == eProtocolType::UDP; }
		bool									IsConnected() { return m_state == eSessionState::CONNECTED; }

		Sptr<Service>							GetService() { return m_service.lock(); }
		void									SetService(const Sptr<Service>& service) { m_service = service; }

		NetAddress&								GetRemoteNetAddress() { return m_remoteAddress; }
		void									SetRemoteNetAddress(const NetAddress& address) { m_remoteAddress = address; }
		SOCKET									GetSocket() { return m_socket; }

		Sptr<Session>							GetSession() { return static_pointer_cast<Session>(shared_from_this()); }
		eSessionState							GetState() { return m_state; }

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
