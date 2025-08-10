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





	void HandshakeManager::SendConnectSyn() const
	{
		auto buf = PacketBuilder::CreateHandshakePacket(eHandshakePacketId::CONNECT_SYN);
		m_ownerSession->SendDirect(buf);
	}

	void HandshakeManager::OnReceiveConnectSyn()
	{
		SendConnectSynAck();
	}

	void HandshakeManager::SendConnectSynAck() const
	{
		auto buf = PacketBuilder::CreateHandshakePacket(eHandshakePacketId::CONNECT_SYNACK);
		m_ownerSession->SendDirect(buf);
	}

	void HandshakeManager::OnReceiveConnectSynAck()
	{
		SendConnectAck();
	}

	void HandshakeManager::SendConnectAck() const
	{
		auto buf = PacketBuilder::CreateHandshakePacket(eHandshakePacketId::CONNECT_ACK);
		m_ownerSession->SendDirect(buf);
	}

	void HandshakeManager::OnReceiveConnectAck()
	{
	}

	void HandshakeManager::SendDisconnectFin() const
	{
		auto buf = PacketBuilder::CreateHandshakePacket(eHandshakePacketId::DISCONNECT_FIN);
		m_ownerSession->SendDirect(buf);
	}

	void HandshakeManager::OnReceiveDisconnectFin()
	{

	}

	void HandshakeManager::SendDisconnectFinAck() const
	{
		auto buf = PacketBuilder::CreateHandshakePacket(eHandshakePacketId::DISCONNECT_FINACK);
		m_ownerSession->SendDirect(buf);
	}

	void HandshakeManager::OnReceiveDisconnectFinAck()
	{
	}

	void HandshakeManager::SendDisconnectAck() const
	{
		auto buf = PacketBuilder::CreateHandshakePacket(eHandshakePacketId::DISCONNECT_ACK);
		m_ownerSession->SendDirect(buf);
	}

	void HandshakeManager::OnReceiveDisconnectAck()
	{
	}
}
