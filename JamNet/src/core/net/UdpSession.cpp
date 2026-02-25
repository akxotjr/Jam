#include "pch.h"
#include "jamnet/core/net/UdpSession.h"
#include "jamnet/core/net/SessionSystems.h"

namespace jam::net
{
	UdpSession::UdpSession()
	{
		m_protocol = eProtocolType::UDP;
	}


	bool UdpSession::Connect()
	{
		GetService()->RegisterHandshakingUdpSession(static_pointer_cast<UdpSession>(shared_from_this()));
		
		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		Post(Job([self]
			{
				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;
				ConnectHandshake(e);
			}, eJobPriority::CTRL));

		return true;
	}

	void UdpSession::Disconnect()
	{
		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		Post(Job([self]
			{
				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;
				DisconnectHandshake(e);
			}, eJobPriority::CTRL));
	}

	void UdpSession::Send(const shared_ptr<SendBuffer>& buf)
	{
		if (!buf || !buf->Buffer())
			return;

		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		Post(Job([self, buf]
			{
				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;
				
				SendPacketToSession(e, buf);
			}));
	}

	void UdpSession::OnLinkEstablished()
	{
		GetService()->CompleteUdpHandshake(m_remoteAddress);

		m_state.store(eSessionState::CONNECTED, std::memory_order_relaxed);
		OnConnected();
	}

	void UdpSession::OnLinkTerminated()
	{
		OnDisconnected();

		m_state.store(eSessionState::DISCONNECTED, std::memory_order_relaxed);

		GetService()->ReleaseUdpSession(static_pointer_cast<UdpSession>(shared_from_this()));
	}



	void UdpSession::ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer)
	{
		if (!recvBuffer.OnWrite(numOfBytes))
			return;

		BYTE* data = recvBuffer.ReadPos();
		const int32 size = numOfBytes;

		shared_ptr<RecvBuffer> buf = RecvBuffer::FromSpan(data, size);

		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		Post(Job([self, buf]
			{
				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;

				ProcessReceivedPacket(e, buf);
			}));

		recvBuffer.OnRead(size);
		recvBuffer.Clean();
	}

	void UdpSession::HandleError(int32 errorCode)
	{
		switch (errorCode)
		{
		case WSAECONNRESET:
		case WSAECONNABORTED:
			Disconnect();
			break;
		default:
			JAMNET_LOG_ERROR("WINERROR: ", errorCode);
			break;
		}
	}

	void UdpSession::RegisterSend(const vector<shared_ptr<SendBuffer>>& bufs)
	{
		GetService()->m_udpRouter->RegisterSend(bufs, GetRemoteNetAddress());
	}

}
