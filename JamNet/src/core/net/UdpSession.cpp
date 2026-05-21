#include "pch.h"
#include "jamnet/core/net/UdpSession.h"

#include "jambase/EnumUtils.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/net/WinErrorHandling.h"

namespace jam::net
{
	struct UdpPrebindConnectRetry
	{
		static void Submit(EndpointHandle handle, uint32 retriesLeft);
	};

	namespace
	{
		inline constexpr uint64 kClientBindRetryDelayNs = 500_ms;
		inline const RouteDomain kBoundSessionRouteDomain = RouteDomain::From("BoundSession");

		RouteKey MakeBoundSessionRouteKey(uint64 accountId)
		{
			return GLOBAL_EXEC.MakeRouteKey(kBoundSessionRouteDomain, accountId);
		}

		void SendUdpBindResponse(Session& session, bool success, uint64 accountId, RuntimeId userId)
		{
			const UDP_BIND_RES_DATA res{ .accountId = accountId, .userId = userId, .success = static_cast<uint8>(success ? 1 : 0) };
			auto pkt = PacketBuilder::CreateSystemPacket(eSystemPacketId::UDP_BIND_RES, PacketFlags::NONE, eChannel::UNRELIABLE_UNORDERED, &res, sizeof(res));
			session.Send(pkt);
		}

		bool IsUdpBindPacket(const PacketHeaderView& view)
		{
			if (!view.IsValid() || view.Type() != ePacketType::SYSTEM)
				return false;

			const auto id = ToEnum<eSystemPacketId>(view.Id());
			return id == eSystemPacketId::UDP_BIND_REQ || id == eSystemPacketId::UDP_BIND_RES;
		}
	}


	UdpSession::UdpSession()
	{
		m_protocol = eProtocolType::UDP;
	}


	bool UdpSession::Connect()
	{
		auto* service = GetService();
		if (!service)
			return false;

		if (IsPreBindPhase())
		{
			ProcessConnect();
			return true;
		}

		return false;
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

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (view.IsValid() && view.TotalSize() == packet->Size() && view.Type() == ePacketType::SYSTEM
			&& (IsUdpBindPacket(view) || GetEntity() == entt::null || !CanCreateSessionEntity()))
		{
			SendImmediatePacket(packet);
			return;
		}

		const EndpointHandle endpointHandle = GetEndpointHandle();
		const SessionId sessionId = GetSessionId();
		Post(Job([endpointHandle, sessionId, packet]
			{
				auto& state = GetOrCreateSessionShardState();
				auto* self = static_cast<UdpSession*>(state.FindSessionAny(sessionId, endpointHandle));
				if (!self)
					return;

				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;

				SendPacketToSession(e, packet);
			}, eJobPriority::Control));
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
		(void)numOfBytes;
		PacketHeaderView directView = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (directView.IsValid() && directView.TotalSize() == packet->Size() && directView.Type() == ePacketType::SYSTEM)
		{
			ProcessSystemPacket(std::move(packet), directView, ingressRecvTime_ns);
			return;
		}

		const entt::entity e = GetEntity();
		if (e == entt::null)
		{
			JAMNET_LOG_WARN("[UdpSession::ProcessRecv] session id= {}. entity is null", m_sessionId);
			return;
		}

		ProcessReceivedPacket(e, std::move(packet), ingressRecvTime_ns);
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
			win_error::LogWsaError("[UdpSession] socket operation", errorCode);
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
		if (const entt::entity e = GetEntity(); e != entt::null)
		{
			ConnectHandshake(e);
			return;
		}

		if (m_prebindHandshake.state == HandshakeState::CONNECT_SYN_SENT)
			return;
		if (m_prebindHandshake.state != HandshakeState::DISCONNECTED)
			return;

		SendImmediatePacket(PacketBuilder::CreateHandshakePacket(eSystemPacketId::CONNECT_SYN));
		m_prebindHandshake.state		= HandshakeState::CONNECT_SYN_SENT;
		m_prebindHandshake.lastTime_ns	= NOW_NS();
		m_prebindHandshake.retryCount	= 0;
		SchedulePreBindHandshakeRetry();
	}

	void UdpSession::ProcessDisconnect()
	{
		if (const entt::entity e = GetEntity(); e != entt::null)
		{
			DisconnectHandshake(e);
			return;
		}

		if (m_prebindHandshake.state != HandshakeState::CONNECTED && m_prebindHandshake.state != HandshakeState::DISCONNECT_FIN_SENT)
			return;

		SendImmediatePacket(PacketBuilder::CreateHandshakePacket(eSystemPacketId::DISCONNECT_FIN));
		if (m_prebindHandshake.state == HandshakeState::CONNECTED)
			m_prebindHandshake.state = HandshakeState::DISCONNECT_FIN_SENT;
		m_prebindHandshake.lastTime_ns = NOW_NS();
		SchedulePreBindHandshakeRetry();
	}

	void UdpSession::HandlePreBindSystemPacket(const PacketHeaderView& view)
	{
		const uint64 now_ns = NOW_NS();
		switch (ToEnum<eSystemPacketId>(view.Id()))
		{
		case eSystemPacketId::CONNECT_SYN:
			if (m_prebindHandshake.state != HandshakeState::DISCONNECTED)
				return;

			SendImmediatePacket(PacketBuilder::CreateHandshakePacket(eSystemPacketId::CONNECT_SYNACK));
			m_prebindHandshake.state		= HandshakeState::CONNECT_SYNACK_SENT;
			m_prebindHandshake.lastTime_ns	= now_ns;
			m_prebindHandshake.retryCount	= 0;
			SchedulePreBindHandshakeRetry();
			return;

		case eSystemPacketId::CONNECT_SYNACK:
			if (m_prebindHandshake.state != HandshakeState::CONNECT_SYN_SENT)
				return;

			SendImmediatePacket(PacketBuilder::CreateHandshakePacket(eSystemPacketId::CONNECT_ACK));
			m_prebindHandshake.state		= HandshakeState::CONNECTED;
			m_prebindHandshake.lastTime_ns	= now_ns;
			++m_prebindTimerToken;
			SetSessionState(eSessionState::Connected);
			TrySessionBinding();
			return;

		case eSystemPacketId::CONNECT_ACK:
			if (m_prebindHandshake.state != HandshakeState::CONNECT_SYNACK_SENT)
				return;
			m_prebindHandshake.state		= HandshakeState::CONNECTED;
			m_prebindHandshake.lastTime_ns	= now_ns;
			++m_prebindTimerToken;
			SetSessionState(eSessionState::Connected);
			TrySessionBinding();
			return;

		case eSystemPacketId::UDP_BIND_RES:
			if (!IsClientSide())
				return;
			if (view.PayloadSize() < sizeof(UDP_BIND_RES_DATA))
				return;
			if (m_clientBind.bound)
				return;
			if (const auto* res = reinterpret_cast<const UDP_BIND_RES_DATA*>(view.Payload());
				res && res->accountId == m_accountId && res->userId == m_userId)
			{
				m_clientBind.active = false;
				++m_clientBind.timerToken;
				if (!res->success)
				{
					m_clientBind.active = false;
					m_clientBind.bound  = false;
					JAMNET_LOG_ERROR("[UdpSession] UDP bind failed or timed out. accountId={}, userId={}", m_accountId, m_userId);
					Disconnect();
					return;
				}

				m_clientBind.bound = true;
				IssueSessionId();
				if (GetSessionId() == kInvalidSessionId)
				{
					JAMNET_LOG_ERROR("[UdpSession] Failed to issue bound session id. accountId={}, userId={}", m_accountId, m_userId);
					m_clientBind.active = false;
					m_clientBind.bound  = false;
					Disconnect();
					return;
				}
			}
			return;

		case eSystemPacketId::UDP_BIND_REQ:
			if (!IsServerSide())
				return;
			if (view.PayloadSize() < sizeof(UDP_BIND_REQ_DATA))
				return;
			if (const auto* req = reinterpret_cast<const UDP_BIND_REQ_DATA*>(view.Payload()); !req || req->accountId == 0 || req->userId == kInvalidRuntimeId)
			{
				SendUdpBindResponse(*this, false, req ? req->accountId : 0, kInvalidRuntimeId);
				return;
			}
			else
			{
				if (GetSessionId() != kInvalidSessionId)
				{
					const bool samePrincipal = (m_accountId == req->accountId && m_userId == req->userId);
					SendUdpBindResponse(*this, samePrincipal, req->accountId, samePrincipal ? m_userId : kInvalidRuntimeId);
					return;
				}
				if (m_prebindHandshake.state != HandshakeState::CONNECTED)
					return;
				if (!TryBeginServerBind())
					return;

				const RouteKey boundRouteKey = MakeBoundSessionRouteKey(req->accountId);
				const uint64 endpointId = GetEndpointId();
				Rehome(boundRouteKey, [this, accountId = req->accountId, userId = req->userId, endpointId, boundRouteKey](bool rehomeOk) mutable
					{
						if (!rehomeOk || GetEndpointId() != endpointId || GetRouteKey() != boundRouteKey)
						{
							EndServerBind();
							SendUdpBindResponse(*this, false, accountId, kInvalidRuntimeId);
							return;
						}
						if (!ValidateServerUdpBindPrincipal(accountId, userId))
						{
							EndServerBind();
							SendUdpBindResponse(*this, false, accountId, kInvalidRuntimeId);
							return;
						}
						if (m_prebindHandshake.state != HandshakeState::CONNECTED)
						{
							EndServerBind();
							SendUdpBindResponse(*this, false, accountId, kInvalidRuntimeId);
							return;
						}

						SetAccountId(accountId);
						SetUserId(userId);
						IssueSessionId();
						if (GetSessionId() == kInvalidSessionId)
						{
							EndServerBind();
							SendUdpBindResponse(*this, false, accountId, kInvalidRuntimeId);
							return;
						}
						if (IsClosing())
						{
							EndServerBind();
							SendUdpBindResponse(*this, false, accountId, kInvalidRuntimeId);
							return;
						}

						EndServerBind();
						SendUdpBindResponse(*this, true, accountId, m_userId);
					});
			}
			return;

		default: break;
		}
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

	void UdpSession::ProcessSystemPacket(Packet packet, const PacketHeaderView& view, uint64 ingressRecvTime_ns)
	{
		if (!view.IsValid() || view.Type() != ePacketType::SYSTEM)
			return;

		if (IsPreBindPhase())
		{
			HandlePreBindSystemPacket(view);
			return;
		}

		if (IsServerSide() && ToEnum<eSystemPacketId>(view.Id()) == eSystemPacketId::UDP_BIND_REQ)
		{
			HandlePreBindSystemPacket(view);
			return;
		}

		if (const entt::entity e = GetEntity(); e != entt::null)
		{
			auto& L = CurrentShardLocalChecked();
			RecvContext ctx{
				.L				= L,
				.e				= e,
				.view			= view,
				.packet			= std::move(packet),
				.now_ns			= NOW_NS(),
				.ingressTime_ns = ingressRecvTime_ns
			};

			HandlePostBindSystemPacket(ctx);
		}
	}


	void UdpSession::SchedulePreBindHandshakeRetry()
	{
		const uint32 token = ++m_prebindTimerToken;
		const EndpointHandle endpointHandle = GetEndpointHandle();
		SubmitAfter(Job([endpointHandle, token]()
			{
				auto& state = GetOrCreateSessionShardState(CurrentShardLocalChecked());
				auto* self = static_cast<UdpSession*>(state.FindPreboundSession(endpointHandle));
				if (!self)
					return;
				if (!self->IsPreBindPhase())
					return;
				if (self->m_prebindTimerToken != token)
					return;

				auto& hs = self->m_prebindHandshake;
				const uint64 now_ns = NOW_NS();
				if (now_ns - hs.lastTime_ns < HandshakeState::Timeout_ns)
				{
					self->SchedulePreBindHandshakeRetry();
					return;
				}

				eSystemPacketId resendId;
				switch (hs.state)
				{
				case HandshakeState::CONNECT_SYN_SENT:
					resendId = eSystemPacketId::CONNECT_SYN;
					break;
				case HandshakeState::CONNECT_SYNACK_SENT:
					resendId = eSystemPacketId::CONNECT_SYNACK;
					break;
				case HandshakeState::DISCONNECT_FIN_SENT:
					resendId = eSystemPacketId::DISCONNECT_FIN;
					break;
				case HandshakeState::DISCONNECT_FINACK_SENT:
				case HandshakeState::CLOSING:
					resendId = eSystemPacketId::DISCONNECT_FINACK;
					break;
				default:
					return;
				}

				if (hs.retryCount >= HandshakeState::MaxRetry)
				{
					self->AbortPreBindHandshake();
					return;
				}

				self->SendImmediatePacket(PacketBuilder::CreateHandshakePacket(resendId));
				hs.retryCount++;
				hs.lastTime_ns = now_ns;
				self->SchedulePreBindHandshakeRetry();
			}, eJobPriority::Control), HandshakeState::Timeout_ns);
	}

	void UdpSession::AbortPreBindHandshake()
	{
		m_prebindHandshake.state = HandshakeState::TIME_OUT;
		++m_prebindTimerToken;
		m_clientBind.active = false;
		++m_clientBind.timerToken;
		SetSessionState(eSessionState::Disconnected);
		NotifyLinkTerminatedIfEstablished();
		OnDisconnected();
		MarkClosing();
		m_releaseQueued.store(true, std::memory_order_release);
		if (GetPendingDispatchCount() == 0)
			OnPendingDispatchDrained();
	}

	void UdpSession::TrySessionBinding()
	{
		if (!IsClientSide() || !IsConnected())
			return;
		if (m_clientBind.active || m_clientBind.bound)
			return;
		if (m_accountId == 0)
			return;

		m_clientBind.active		= true;
		m_clientBind.bound		= false;
		m_clientBind.retryCount = 0;
		SetSessionState(eSessionState::Binding);

		const UDP_BIND_REQ_DATA req{ .accountId = m_accountId, .userId = m_userId };
		auto packet = PacketBuilder::CreateSystemPacket(eSystemPacketId::UDP_BIND_REQ, PacketFlags::NONE, eChannel::UNRELIABLE_UNORDERED, &req, sizeof(req));
		Send(packet);
		ScheduleSessionBindingRetry();
	}

	void UdpSession::ScheduleSessionBindingRetry()
	{
		const uint32 token = ++m_clientBind.timerToken;
		const EndpointHandle endpointHandle = GetEndpointHandle();
		SubmitAfter(Job([endpointHandle, token]()
			{
				auto& state = GetOrCreateSessionShardState();
				auto* self = static_cast<UdpSession*>(state.FindPreboundSession(endpointHandle));
				if (!self)
					return;
				if (!self->m_clientBind.active || self->m_clientBind.bound || self->m_clientBind.timerToken != token)
					return;
				if (self->m_clientBind.retryCount >= HandshakeState::MaxRetry)
				{
					self->m_clientBind.active = false;
					self->m_clientBind.bound  = false;
					JAMNET_LOG_ERROR("[UdpSession] UDP bind failed or timed out. accountId={}, userId={}", self->m_accountId, self->m_userId);
					self->Disconnect();
					return;
				}

				self->m_clientBind.retryCount++;
				const UDP_BIND_REQ_DATA req{ .accountId = self->m_accountId, .userId = self->m_userId };
				auto pkt = PacketBuilder::CreateSystemPacket(eSystemPacketId::UDP_BIND_REQ, PacketFlags::NONE, eChannel::UNRELIABLE_UNORDERED, &req, sizeof(req));
				self->Send(pkt);
				self->ScheduleSessionBindingRetry();
			}, eJobPriority::Control), kClientBindRetryDelayNs);
	}


	void UdpSession::SendImmediatePacket(Packet packet)
	{
		if (!packet.IsValid())
			return;

		PacketChain chain;
		chain.Add(packet);

		std::vector<PacketChain> chains;
		chains.push_back(std::move(chain));
		RegisterSend(std::move(chains));
	}
}
