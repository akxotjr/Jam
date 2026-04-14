#include "pch.h"
#include "jamnet/core/net/UdpSession.h"
#include "jamnet/core/net/PacketBuilder.h"
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
			}, eJobPriority::Control));

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
			}, eJobPriority::Control));
	}

	void UdpSession::Send(const std::shared_ptr<SendBuffer>& buf)
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

		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		auto* service = GetService();
		if (!service)
			return;

		if (auto connected = service->FindSessionInConnected(m_remoteAddress); connected.get() == this)
		{
			service->ReleaseUdpSession(self);
			return;
		}

		if (auto handshaking = service->FindSessionInHandshaking(m_remoteAddress); handshaking.get() == this)
		{
			service->ReleaseHandshakingUdpSession(self);
		}
	}



	void UdpSession::ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer, uint64 ingressRecvTime_ns)
	{
		if (!recvBuffer.OnWrite(numOfBytes))
			return;

		const BYTE*  data = recvBuffer.ReadPos();
		const int32  size = numOfBytes;
		eJobPriority priority = eJobPriority::Normal;
		if (size >= static_cast<int32>(PacketHeader::BASE_SIZE))
		{
			const auto* header = reinterpret_cast<const PacketHeader*>(recvBuffer.ReadPos());
			if (header->IsValid() && header->GetGroup() == ePacketGroup::CTRL)
				priority = eJobPriority::Control;
		}

		std::shared_ptr<RecvBuffer> buf = RecvBuffer::FromSpan(data, size);

		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		Post(Job([self, buf, ingressRecvTime_ns]
			{
				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;

				ProcessReceivedPacket(e, buf, ingressRecvTime_ns);
			}, priority));

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

	void UdpSession::RegisterSend(const std::vector<std::shared_ptr<SendBuffer>>& bufs)
	{
		GetService()->m_udpRouter->RegisterSend(bufs, GetRemoteNetAddress());
	}

}
