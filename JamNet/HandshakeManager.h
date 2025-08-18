#pragma once

namespace jam::net
{
	//	정상 종료(능동 종료측) :
	//	CONNECTED->FIN_SENT->FINACK_RECEIVED->TIME_WAIT->DISCONNECTED

	//	정상 종료(수동 종료측) :
	//	CONNECTED->FIN_RECEIVED->FINACK_SENT->DISCONNECTED

	//	동시 종료 :
	//	CONNECTED->FIN_SENT->CLOSING->TIME_WAIT->DISCONNECTED


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

		TIME_WAIT,
		CLOSING,	// Simultaneous Close

		TIME_OUT,
		ERROR_STATE,
	
	};

	class HandshakeManager
	{
	public:
		HandshakeManager(UdpSession* owner) : m_owner(owner) {}
		~HandshakeManager() = default;

		// Connection Handshake : 3-way
		bool						InitiateConnection();
		bool						HandleConnectionPacket(eSystemPacketId id);

		// Disconnection Handshake : 4-way
		bool						InitiateDisconnection();
		bool						HandleDisconnectionPacket(eSystemPacketId id);

		bool						IsConnected() const { return m_state == eHandshakeState::CONNECTED; }
		bool						IsDisconnected() const { return m_state == eHandshakeState::DISCONNECTED; }
		eHandshakeState				GetState() const { return m_state; }

		void						TransitionToState(eHandshakeState newState);
		void						CheckTimeout(uint64 currentTick);
		void						HandleTimeout();
		void						RetryCurrentHandshake();

		// Time Wait
		void						Update();
		bool						IsInTimeWait() const { return m_state == eHandshakeState::TIME_WAIT; }
		bool						CanAcceptNewConnection() const;
 
	private:

		// Connection handshake methods
		void						SendConnectSyn();
		void						OnReceiveConnectSyn();
		void						SendConnectSynAck();
		void						OnReceiveConnectSynAck();
		void						SendConnectAck();
		void						OnReceiveConnectAck();

		// Disconnection handshake methods
		void						SendDisconnectFin();
		void						OnReceiveDisconnectFin();
		void						SendDisconnectFinAck();
		void						OnReceiveDisconnectFinAck();
		void						SendDisconnectAck();
		void						OnReceiveDisconnectAck();

		// Time Wait
		void						EnterTimeWait();
		void						HandleTimeWaitTimeout();
		bool						IsTimeWaitExpired(uint64 currentTick) const;


	private:
		UdpSession*					m_owner;

		Atomic<eHandshakeState>		m_state{ eHandshakeState::DISCONNECTED };
		Atomic<uint64>				m_lastHandshakeTime{ 0 };
		Atomic<uint32>				m_retryCount{ 0 };
		Atomic<uint64>				m_timeWaitStartTime{ 0 };

		static constexpr uint32		MAX_RETRY_COUNT = 5;			// TODO: TUNE THE VALUE
		static constexpr uint64		TIMEOUT_INTERVAL = 5000; 
		static constexpr uint64		DEFAULT_TIMEOUT = 5000;

		static constexpr uint64		MSL = 30000;					// Maximal Segment Lifetime
		static constexpr uint64		TIME_WAIT_DURATION = 2 * MSL;	
	};
}

