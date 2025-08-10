#pragma once

namespace jam::net
{
	enum class eHandshakeState : uint8
	{
		DISCONNECTED,

		CONNECT_SYN_SENT,
		CONNECT_SYN_RECEIVED,
		CONNECT_SYNACK_SENT,
		CONNECT_SYNACK_RECEIVED,
		CONNECTED,

		DISCONNECT_FIN_SENT,
		DISCONNECT_FIN_RECEIVED,
		DISCONNECT_FINACK_SENT,
		DISCONNECT_FINACK_RECEIVED,
		DISCONNECT_ACK_SENT,
		DISCONNECT_ACK_RECEIVED,

		TIMEOUT,
		ERROR_STATE,
	
	};

	enum class eHandshakePacketId : uint8
	{
		CONNECT_SYN = 1,
		CONNECT_SYNACK = 2,
		CONNECT_ACK = 3,

		DISCONNECT_FIN = 4,
		DISCONNECT_FINACK = 5,
		DISCONNECT_ACK = 6,
	};

	class HandshakeManager
	{
	public:
		HandshakeManager() = default;
		~HandshakeManager() = default;

		// Connection Handshake : 3-way
		bool InitiateConnection();
		bool HandleConnectionPacket();

		// Disconnection Handshake : 4-way
		bool InitiateDisconnection();
		bool HandleDisconnectionPacket();

		bool IsConnected() const { return m_state == eHandshakeState::CONNECTED; }
		bool IsDisconnected() const { return m_state == eHandshakeState::DISCONNECTED; }
		eHandshakeState GetState() const { return m_state; }

	private:

		// Connection handshake methods
		void SendConnectSyn() const;
		void OnReceiveConnectSyn();
		void SendConnectSynAck() const;
		void OnReceiveConnectSynAck();
		void SendConnectAck() const;
		void OnReceiveConnectAck();

		// Disconnection handshake methods
		void SendDisconnectFin() const;
		void OnReceiveDisconnectFin();
		void SendDisconnectFinAck() const;
		void OnReceiveDisconnectFinAck();
		void SendDisconnectAck() const;
		void OnReceiveDisconnectAck();


	private:
		UdpSession* m_ownerSession;

		eHandshakeState m_state = eHandshakeState::DISCONNECTED;

		uint64 m_lastHandshakeTime = 0;
		uint32 m_retryCount = 0;

		static constexpr uint32 MAX_RETRY_COUNT = 5;
		static constexpr uint64 TIMEOUT_INTERVAL = 5000; 
		static constexpr uint64 DEFAULT_TIMEOUT = 5000;
	};
}

