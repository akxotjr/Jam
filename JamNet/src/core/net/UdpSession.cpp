#include "pch.h"
#include "jamnet/core/net/UdpSession.h"

#include "jamnet/core/executor/Job.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/SessionSystems.h"

namespace jam::net
{

	UdpSession::UdpSession()
	{
		m_protocol = eProtocolType::UDP;
	}


	bool UdpSession::Connect()
	{
		auto* service = GetService();
		if (!service)
			return false;

		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		const bool registered = service->RegisterHandshakingUdpSession(self);
		if (!registered)
			return false;

		if (RegisterConnect())
			return true;

		service->ReleaseHandshakingUdpSession(self);
		return false;
	}

	void UdpSession::Disconnect()
	{
		(void)RegisterDisconnect();
	}

	void UdpSession::Send(Packet packet)
	{
		if (!packet.IsValid())
			return;

		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		Post(Job([self, packet]
			{
				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;
				
				SendPacketToSession(e, packet);
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

	void UdpSession::Dispatch(IocpEvent* iocpEvent, int32 /*numOfBytes*/)
	{
		if (!iocpEvent)
			return;

		switch (iocpEvent->m_eventType)
		{
		case eEventType::UdpConnect:
			ProcessConnect();
			break;

		case eEventType::UdpDisconnect:
			ProcessDisconnect();
			break;

		default:
			break;
		}
	}



	void UdpSession::ProcessRecv(int32 numOfBytes, Packet packet, uint64 ingressRecvTime_ns)
	{
		const int32  size = numOfBytes;
		eJobPriority priority = eJobPriority::Normal;
		if (size >= static_cast<int32>(PacketHeader::BASE_SIZE))
		{
			const auto* header = reinterpret_cast<const PacketHeader*>(packet->Head());
			if (header->IsValid() && header->GetGroup() == ePacketGroup::CTRL)
				priority = eJobPriority::Control;
		}

		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		Post(Job([self, pkt = std::move(packet), ingressRecvTime_ns]
			{
				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;

				ProcessReceivedPacket(e, pkt, ingressRecvTime_ns);
			}, priority));
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

	void UdpSession::RegisterSend(std::vector<PacketChain>&& chains)
	{
		GetService()->m_udpRouter->RegisterSend(std::move(chains), GetRemoteNetAddress());
	}

	bool UdpSession::RegisterConnect()
	{
		auto* service = GetService();
		if (!service || !service->GetIocpCore())
			return false;

		m_connectEvent.Init();
		m_connectEvent.m_owner = shared_from_this();

		if (!service->GetIocpCore()->Post(&m_connectEvent))
		{
			m_connectEvent.m_owner = nullptr;
			return false;
		}

		return true;
	}

	bool UdpSession::RegisterDisconnect()
	{
		auto* service = GetService();
		if (!service || !service->GetIocpCore())
			return false;

		m_disconnectEvent.Init();
		m_disconnectEvent.m_owner = shared_from_this();

		if (!service->GetIocpCore()->Post(&m_disconnectEvent))
		{
			m_disconnectEvent.m_owner = nullptr;
			return false;
		}

		return true;
	}

	void UdpSession::ProcessConnect()
	{
		m_connectEvent.m_owner = nullptr;

		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		Post(Job([self]
			{
				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;
				ConnectHandshake(e);
			}, eJobPriority::Control));
	}

	void UdpSession::ProcessDisconnect()
	{
		m_disconnectEvent.m_owner = nullptr;

		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		Post(Job([self]
			{
				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;
				DisconnectHandshake(e);
			}, eJobPriority::Control));
	}

}
