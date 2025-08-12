#include "pch.h"
#include "HandshakeManager.h"
#include "Clock.h"

namespace jam::net
{
	bool HandshakeManager::InitiateConnection()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::DISCONNECTED)
			return false;

		SendConnectSyn();
		return true;
	}

	bool HandshakeManager::HandleConnectionPacket(eSystemPacketId id)
	{
		switch (id)
		{
		case eSystemPacketId::CONNECT_SYN:
			OnReceiveConnectSyn();
			return true;
		case eSystemPacketId::CONNECT_SYNACK:
			OnReceiveConnectSynAck();
			return true;
		case eSystemPacketId::CONNECT_ACK:
			OnReceiveConnectAck();
			return true;
		default:
			return false;
		}
	}

	bool HandshakeManager::InitiateDisconnection()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::CONNECTED)
			return false;

		SendDisconnectFin();
		return true;
	}

	bool HandshakeManager::HandleDisconnectionPacket(eSystemPacketId id)
	{
		switch (id)
		{
		case eSystemPacketId::DISCONNECT_FIN:
			OnReceiveDisconnectFin();
			return true;
		case eSystemPacketId::DISCONNECT_FINACK:
			OnReceiveDisconnectFinAck();
			return true;
		case eSystemPacketId::DISCONNECT_ACK:
			OnReceiveDisconnectAck();
			return true;
		default:
			return false;
		}
	}


	void HandshakeManager::Update(uint64 currentTick)
	{
		switch (m_state)
		{
		case eHandshakeState::TIME_WAIT:
			if (IsTimeWaitExpired(currentTick))
			{
				HandleTimeWaitTimeout();
			}
			break;
		case eHandshakeState::CONNECT_SYN_SENT:
		case eHandshakeState::CONNECT_SYNACK_SENT:
		case eHandshakeState::DISCONNECT_FIN_SENT:
		case eHandshakeState::DISCONNECT_FINACK_SENT:
			CheckTimeout(currentTick);
			break;
		default:
			break;
		}
	}

	bool HandshakeManager::CanAcceptNewConnection() const
	{
		return m_state == eHandshakeState::DISCONNECTED;
	}

	void HandshakeManager::TransitionToState(eHandshakeState newState)
	{
		eHandshakeState oldState = m_state.exchange(newState);

		if (oldState == newState)
			return;

		switch (newState)
		{
		case eHandshakeState::CONNECTED:
			m_ownerSession->OnHandshakeCompleted(true);
			m_retryCount.store(0);
			break;
		case eHandshakeState::DISCONNECTED:
			if (oldState != eHandshakeState::TIME_WAIT)	// TIME_WAIT 에서 전환되지 않은 경우에만 콜백 호출
			{
				m_ownerSession->OnHandshakeCompleted(false);
			}
			m_retryCount.store(0);
			break;
		case eHandshakeState::TIME_OUT:
		case eHandshakeState::ERROR_STATE:
			m_ownerSession->OnHandshakeCompleted(false);
			break;
		default:
			break;
		}
	}

	void HandshakeManager::CheckTimeout(uint64 currentTick)
	{
		uint64 lastTime = m_lastHandshakeTime.load();
		if (lastTime == 0)
			return;

		if ((currentTick - m_lastHandshakeTime) >= TIMEOUT_INTERVAL)
		{
			HandleTimeout();
		}
	}

	void HandshakeManager::HandleTimeout()
	{
		uint32 currentRetryCount = m_retryCount.load();
		if (currentRetryCount >= MAX_RETRY_COUNT)
		{
			TransitionToState(eHandshakeState::TIME_OUT);
			return;
		}

		RetryCurrentHandshake();
	}

	void HandshakeManager::RetryCurrentHandshake()
	{
		m_retryCount.fetch_add(1);

		switch (eHandshakeState state = m_state.load())
		{
		case eHandshakeState::CONNECT_SYN_SENT:
			SendConnectSyn();
			break;
		case eHandshakeState::CONNECT_SYNACK_SENT:
			SendConnectSynAck();
			break;
		case eHandshakeState::DISCONNECT_FIN_SENT:
			SendDisconnectFin();
			break;
		case eHandshakeState::DISCONNECT_FINACK_SENT:
			SendDisconnectFinAck();
			break;
		case eHandshakeState::CLOSING:
			SendDisconnectFinAck();
			break;
		default:
			break;
		}
	}


	void HandshakeManager::SendConnectSyn()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::DISCONNECTED && state != eHandshakeState::CONNECT_SYN_SENT)
			return;

		auto buf = PacketBuilder::CreateHandshakePacket(eSystemPacketId::CONNECT_SYN);
		m_ownerSession->SendDirect(buf);
		if (state == eHandshakeState::DISCONNECTED)
		{
			TransitionToState(eHandshakeState::CONNECT_SYN_SENT);
		}

		m_lastHandshakeTime.store(Clock::Instance().GetCurrentTick());
	}

	void HandshakeManager::OnReceiveConnectSyn()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::DISCONNECTED)
			return;

		TransitionToState(eHandshakeState::CONNECT_SYN_RECEIVED);
		SendConnectSynAck();
	}

	void HandshakeManager::SendConnectSynAck()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::CONNECT_SYN_RECEIVED && state != eHandshakeState::CONNECT_SYNACK_SENT)
			return;

		auto buf = PacketBuilder::CreateHandshakePacket(eSystemPacketId::CONNECT_SYNACK);
		m_ownerSession->SendDirect(buf);
		if (state == eHandshakeState::CONNECT_SYN_RECEIVED)
		{
			TransitionToState(eHandshakeState::CONNECT_SYNACK_SENT);
		}

		m_lastHandshakeTime.store(Clock::Instance().GetCurrentTick());
	}

	void HandshakeManager::OnReceiveConnectSynAck()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::CONNECT_SYN_SENT)
			return;

		TransitionToState(eHandshakeState::CONNECT_SYNACK_RECEIVED);
		SendConnectAck();
	}

	void HandshakeManager::SendConnectAck()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::CONNECT_SYNACK_RECEIVED)
			return;

		auto buf = PacketBuilder::CreateHandshakePacket(eSystemPacketId::CONNECT_ACK);
		m_ownerSession->SendDirect(buf);
		TransitionToState(eHandshakeState::CONNECTED);

		m_lastHandshakeTime.store(Clock::Instance().GetCurrentTick());
	}

	void HandshakeManager::OnReceiveConnectAck()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::CONNECT_SYNACK_SENT)
			return;

		TransitionToState(eHandshakeState::CONNECTED);
	}



	void HandshakeManager::SendDisconnectFin()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::CONNECTED && state != eHandshakeState::DISCONNECT_FIN_SENT)
			return;

		auto buf = PacketBuilder::CreateHandshakePacket(eSystemPacketId::DISCONNECT_FIN);
		m_ownerSession->SendDirect(buf);
		if (state == eHandshakeState::CONNECTED)
		{
			TransitionToState(eHandshakeState::DISCONNECT_FIN_SENT);
		}

		m_lastHandshakeTime.store(Clock::Instance().GetCurrentTick());
	}

	void HandshakeManager::OnReceiveDisconnectFin()
	{
		switch (eHandshakeState state = m_state.load())
		{
		case eHandshakeState::CONNECTED:
			TransitionToState(eHandshakeState::DISCONNECT_FIN_RECEIVED);
			SendDisconnectFinAck();
			break;
		case eHandshakeState::DISCONNECT_FIN_SENT:	// 동시 종료
			TransitionToState(eHandshakeState::CLOSING);
			SendDisconnectFinAck();
			break;
		case eHandshakeState::TIME_WAIT:	// TIME_WAIT 중 중복 FIN 수신 - 다시 ACK 전송
			SendDisconnectAck();
			break;
		default:
			break;
		}
	}

	void HandshakeManager::SendDisconnectFinAck()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::DISCONNECT_FIN_SENT && state != eHandshakeState::DISCONNECT_FINACK_SENT)
			return;

		auto buf = PacketBuilder::CreateHandshakePacket(eSystemPacketId::DISCONNECT_FINACK);
		m_ownerSession->SendDirect(buf);

		if (state == eHandshakeState::DISCONNECT_FIN_RECEIVED)
		{
			TransitionToState(eHandshakeState::DISCONNECT_FINACK_SENT);
		}

		m_lastHandshakeTime.store(Clock::Instance().GetCurrentTick());
	}

	void HandshakeManager::OnReceiveDisconnectFinAck()
	{
		switch (eHandshakeState state = m_state.load())
		{
		case eHandshakeState::DISCONNECT_FIN_SENT:
			TransitionToState(eHandshakeState::DISCONNECT_FINACK_RECEIVED);
			SendDisconnectAck();
			break;
		case eHandshakeState::CLOSING:	// 동시 종료에서 FINACK 수신
			SendDisconnectAck();
			EnterTimeWait();
			break;
		default:
			break;
		}
	}

	void HandshakeManager::SendDisconnectAck()
	{
		eHandshakeState state = m_state.load();
		if (state != eHandshakeState::DISCONNECT_FINACK_RECEIVED)
			return;

		auto buf = PacketBuilder::CreateHandshakePacket(eSystemPacketId::DISCONNECT_ACK);
		m_ownerSession->SendDirect(buf);

		if (state == eHandshakeState::DISCONNECT_FINACK_RECEIVED)
		{
			EnterTimeWait();	// 능동 종료측에서 마지막 ACK 전송 후 TIME_WAIT 진입
		}

		m_lastHandshakeTime.store(Clock::Instance().GetCurrentTick());
	}

	void HandshakeManager::OnReceiveDisconnectAck()
	{
		switch (eHandshakeState state = m_state.load())
		{
		case eHandshakeState::DISCONNECT_FINACK_SENT:
			// 수동 종료측에서 ACK 수신 - 즉시 종료
			TransitionToState(eHandshakeState::DISCONNECTED);
			m_ownerSession->OnHandshakeCompleted(false); // 연결 종료 완료
			break;

		case eHandshakeState::TIME_WAIT:
			// TIME_WAIT 중 중복 ACK - 무시 (타이머 리셋하지 않음)
			break;
		default:
			break;
		}
	}

	void HandshakeManager::EnterTimeWait()
	{
		TransitionToState(eHandshakeState::TIME_WAIT);
		m_timeWaitStartTime.store(Clock::Instance().GetCurrentTick());
	}

	void HandshakeManager::HandleTimeWaitTimeout()
	{
		TransitionToState(eHandshakeState::DISCONNECTED);
		m_ownerSession->OnHandshakeCompleted(false); // 연결 종료 완료
	}

	bool HandshakeManager::IsTimeWaitExpired(uint64 currentTick) const
	{
		return (currentTick - m_timeWaitStartTime) >= TIME_WAIT_DURATION;
	}
}
