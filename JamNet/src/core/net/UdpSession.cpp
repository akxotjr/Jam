#include "pch.h"
#include "jamnet/core/net/UdpSession.h"

#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/ThreadContext.h"
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

		const SessionHandle handle = GetSessionHandle();
		Post(Job([handle]
			{
				auto& L = CurrentShardLocalChecked();
				auto* self = static_cast<UdpSession*>(FindSessionByHandle(L, handle));
				if (!self)
					return;

				const entt::entity e = self->EnsureSessionEntity();
				if (e == entt::null)
					return;

				ConnectHandshake(e);
			}, eJobPriority::Critical));

		return true;
	}

	void UdpSession::Disconnect()
	{
		if (IsClosing())
			return;

		const bool posted = RegisterDisconnect();
		MarkClosing();
		if (!posted)
		{
			m_releaseQueued.store(true, std::memory_order_release);
			if (GetPendingDispatchCount() == 0)
				OnPendingDispatchDrained();
		}
	}

	void UdpSession::Send(Packet packet)
	{
		if (!packet.IsValid())
			return;

		const SessionHandle handle = GetSessionHandle();
		Post(Job([handle, packet]
			{
				auto& L = CurrentShardLocalChecked();
				auto* self = static_cast<UdpSession*>(FindSessionByHandle(L, handle));
				if (!self)
					return;

				const entt::entity e = self->EnsureSessionEntity();
				if (e == entt::null) return;
				
				SendPacketToSession(e, packet);
			}));
	}

	void UdpSession::OnLinkEstablished()
	{
		m_state.store(eSessionState::CONNECTED, std::memory_order_relaxed);
		OnConnected();
	}

	void UdpSession::OnLinkTerminated()
	{
		OnDisconnected();

		m_state.store(eSessionState::DISCONNECTED, std::memory_order_relaxed);
		MarkClosing();
		m_releaseQueued.store(true, std::memory_order_release);
		if (GetPendingDispatchCount() == 0)
			OnPendingDispatchDrained();
	}

	void UdpSession::Dispatch(IocpEvent* iocpEvent, int32 /*bytes*/)
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
		const entt::entity e = EnsureSessionEntity();
		if (e == entt::null) return;

		ProcessReceivedPacket(e, packet, ingressRecvTime_ns);
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
		if (!service->GetIocpCore()->Post(this, &m_connectEvent))
			return false;

		return true;
	}

	bool UdpSession::RegisterDisconnect()
	{
		auto* service = GetService();
		if (!service || !service->GetIocpCore())
			return false;

		m_disconnectEvent.Init();
		if (!service->GetIocpCore()->Post(this, &m_disconnectEvent))
			return false;

		return true;
	}

	void UdpSession::ProcessConnect()
	{
		const SessionHandle handle = GetSessionHandle();
		Post(Job([handle]
			{
				auto& L = CurrentShardLocalChecked();
				auto* self = static_cast<UdpSession*>(FindSessionByHandle(L, handle));
				if (!self)
					return;

				const entt::entity e = self->EnsureSessionEntity();
				if (e == entt::null) return;

				ConnectHandshake(e);
			}, eJobPriority::Control));
	}

	void UdpSession::ProcessDisconnect()
	{
		const SessionHandle handle = GetSessionHandle();
		Post(Job([handle]
			{
				auto& L = CurrentShardLocalChecked();
				auto* self = static_cast<UdpSession*>(FindSessionByHandle(L, handle));
				if (!self)
					return;

				const entt::entity e = self->EnsureSessionEntity();
				if (e == entt::null) return;

				DisconnectHandshake(e);
			}, eJobPriority::Control));
	}

	void UdpSession::OnPendingDispatchDrained()
	{
		if (!m_releaseQueued.exchange(false, std::memory_order_acq_rel))
			return;

		auto* service = GetService();
		if (!service)
			return;

		service->ReleaseUdpSession(this);
	}

	entt::entity UdpSession::EnsureSessionEntity()
	{
		entt::entity e = GetEntity();
		if (e == entt::null)
		{
			CreateEntity();
			e = GetEntity();
		}

		return e;
	}
}
