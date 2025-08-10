#include "pch.h"
#include "HandshakeManager.h"

namespace jam::net
{
	bool HandshakeManager::InitiateConnection()
	{
		if (m_state != eHandshakeState::DISCONNECTED)
			return false;

		SendConnectSyn();
		return true;
	}

	bool HandshakeManager::HandleConnectionPacket()
	{
	}

	bool HandshakeManager::InitiateDisconnection()
	{
		if (m_state != eHandshakeState::CONNECTED)
			return false;

		SendDisconnectFin();
		return true;
	}

	bool HandshakeManager::HandleDisconnectionPacket()
	{
	}





	void HandshakeManager::SendConnectSyn()
	{
		auto buf = PacketBuilder::CreateHandshakePacket();
		m_ownerSession->SendDirect(buf);
	}

	void HandshakeManager::OnReceiveConnectSyn()
	{
	}

	void HandshakeManager::SendConnectSynAck()
	{
	}

	void HandshakeManager::OnReceiveConnectSynAck()
	{
	}

	void HandshakeManager::SendConnectAck()
	{
	}

	void HandshakeManager::OnReceiveConnectAck()
	{
	}

	void HandshakeManager::SendDisconnectFin()
	{
	}

	void HandshakeManager::OnReceiveDisconnectFin()
	{
	}

	void HandshakeManager::SendDisconnectFinAck()
	{
	}

	void HandshakeManager::OnReceiveDisconnectFinAck()
	{
	}

	void HandshakeManager::SendDisconnectAck()
	{
	}

	void HandshakeManager::OnReceiveDisconnectAck()
	{
	}
}
