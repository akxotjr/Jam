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
	namespace
	{
		inline constexpr uint64 kControlRetryDelayNs = 500_ms;
		inline constexpr uint64 kControlRetryJitterNs = 50_ms;
		inline constexpr uint64 kControlDeadlineNs = 10_s;

		uint64 MakeTransactionId(const UdpSession& session)
		{
			uint64 value = NOW_NS() ^ session.GetEndpointId() ^ (session.GetAccountId() * 0x9e3779b97f4a7c15ull);
			return value != 0 ? value : 1;
		}

		uint64 RetryDelayNs(uint64 endpointId, uint32 token)
		{
			uint64 value = endpointId ^ (static_cast<uint64>(token) * 0x9e3779b97f4a7c15ull);
			value ^= value >> 30;
			value *= 0xbf58476d1ce4e5b9ull;
			value ^= value >> 27;
			const uint64 window = kControlRetryJitterNs * 2 + 1;
			return kControlRetryDelayNs - kControlRetryJitterNs + (value % window);
		}

		RouteKey MakeBoundSessionRouteKey(uint64 accountId)
		{
			return GLOBAL_EXEC.MakeAffinityRouteKey(accountId);
		}

		bool IsUdpControlPacket(const PacketHeaderView& view)
		{
			if (!view.IsValid() || view.Type() != ePacketType::SYSTEM)
				return false;

			const auto id = ToEnum<eSystemPacketId>(view.Id());
			return id == eSystemPacketId::UDP_BIND_REQ || id == eSystemPacketId::UDP_BIND_RES
				|| id == eSystemPacketId::UDP_BIND_CONFIRM || id == eSystemPacketId::UDP_UNBIND_REQ
				|| id == eSystemPacketId::UDP_UNBIND_RES;
		}
	}


	UdpSession::UdpSession()
	{
		m_protocol = eProtocolType::UDP;
	}


	bool UdpSession::Connect()
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (!IsPreBindPhase() || IsClosing())
			return false;

		ProcessConnect();
		return true;
	}

	void UdpSession::Disconnect()
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (IsClosing())
			return;

		MarkClosing();
		ProcessDisconnect();
	}

	void UdpSession::Send(Packet packet)
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (!packet.IsValid())
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (view.IsValid() && view.TotalSize() == packet->Size() && view.Type() == ePacketType::SYSTEM
			&& (IsUdpControlPacket(view) || GetEntity() == entt::null || !CanCreateSessionEntity()))
		{
			SendImmediatePacket(packet);
			return;
		}

		const entt::entity e = GetEntity();
		if (e == entt::null)
			return;

		SendPacketToSession(e, std::move(packet));
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
		if (!m_bindConfirmed || m_unbindTombstone)
			return;

		const entt::entity e = GetEntity();
		if (e == entt::null)
		{
			JAM_LOG_WARN("[UdpSession::ProcessRecv] session id= {}. entity is null", m_sessionId);
			return;
		}

		ProcessReceivedPacket(e, std::move(packet), ingressRecvTime_ns);
	}

	void UdpSession::RegisterSend(std::vector<PacketChain>&& chains)
	{
		GetService()->m_udpRouter->RegisterSend(std::move(chains), GetRemoteNetAddress());
	}


	void UdpSession::ProcessConnect()
	{
		if (!IsPreBindPhase() || IsClosing())
			return;

		SetSessionState(eSessionState::Connected);
		TrySessionBinding();
	}

	void UdpSession::ProcessDisconnect()
	{
		if (GetSessionId() == kInvalidSessionId || !m_bindConfirmed)
		{
			AbortTransport("disconnect before UDP bind confirmation");
			return;
		}
		if (m_unbindRequestActive || m_unbindTombstone)
			return;

		m_unbindTransactionId = MakeTransactionId(*this);
		m_unbindDeadline_ns   = NOW_NS() + kControlDeadlineNs;
		m_unbindRequestActive = true;
		SendUnbindRequest();
		ScheduleSessionUnbindRetry();
	}

	void UdpSession::HandleUdpControlPacket(const PacketHeaderView& view)
	{
		switch (ToEnum<eSystemPacketId>(view.Id()))
		{
		case eSystemPacketId::UDP_BIND_RES:
			if (!IsClientSide())
				return;
			if (view.PayloadSize() < sizeof(UDP_BIND_RES_DATA))
				return;
			if (const auto* res = reinterpret_cast<const UDP_BIND_RES_DATA*>(view.Payload());
				res && res->accountId == m_accountId && res->userId == m_userId
				&& res->transactionId == m_bindTransactionId)
			{
				if (m_bindConfirmed && res->success && res->sessionId == GetSessionId())
				{
					SendBindConfirm();
					return;
				}
				if (!m_clientBind.active)
					return;
				m_clientBind.active = false;
				++m_clientBind.timerToken;
				if (!res->success || res->sessionId == kInvalidSessionId)
				{
					JAM_LOG_ERROR("[UdpSession] UDP bind rejected. accountId={}, userId={}, transactionId={}", m_accountId, m_userId, m_bindTransactionId);
					AbortTransport("UDP bind rejected");
					return;
				}

				m_clientBind.bound = true;
				if (!AdoptAuthoritativeSessionId(res->sessionId))
				{
					JAM_LOG_ERROR("[UdpSession] Failed to adopt authoritative session id. accountId={}, userId={}, sessionId={}", m_accountId, m_userId, res->sessionId);
					m_clientBind.active = false;
					m_clientBind.bound  = false;
					AbortTransport("failed to adopt UDP session id");
					return;
				}
				SendBindConfirm();
				m_bindConfirmed = true;
				NotifyLinkEstablishedIfReady();
			}
			return;

		case eSystemPacketId::UDP_BIND_REQ:
			if (!IsServerSide())
				return;
			if (view.PayloadSize() < sizeof(UDP_BIND_REQ_DATA))
				return;
			if (const auto* req = reinterpret_cast<const UDP_BIND_REQ_DATA*>(view.Payload());
				!req || req->accountId == 0 || req->userId == kInvalidRuntimeId || req->transactionId == 0)
			{
				return;
			}
			else
			{
				if (GetSessionId() != kInvalidSessionId)
				{
					if (m_accountId == req->accountId && m_userId == req->userId && m_bindTransactionId == req->transactionId)
						SendBindResponse();
					return;
				}
				if (!TryBeginServerBind())
					return;

				const RouteKey boundRouteKey = MakeBoundSessionRouteKey(req->accountId);
				const uint64 endpointId = GetEndpointId();
				Rehome(boundRouteKey, [this, accountId = req->accountId, userId = req->userId, transactionId = req->transactionId, endpointId, boundRouteKey](bool rehomeOk) mutable
					{
						if (!rehomeOk || GetEndpointId() != endpointId || GetRouteKey() != boundRouteKey)
						{
							EndServerBind();
							AbortTransport("UDP bind rehome failed");
							return;
						}
						m_bindTransactionId = transactionId;
						SetAccountId(accountId);
						SetUserId(userId);
						if (!ValidateServerUdpBindPrincipal(accountId, userId))
						{
							EndServerBind();
							SendBindResponse();
							AbortTransport("UDP bind principal validation failed");
							return;
						}

						// UDP_BIND_RES means the server has accepted and promoted the endpoint.
						// CONFIRM only acknowledges that response so the server can stop retrying it.
						m_bindConfirmed = true;
						IssueSessionId();
						if (GetSessionId() == kInvalidSessionId)
						{
							EndServerBind();
							AbortTransport("UDP session id issuance failed");
							return;
						}
						if (IsClosing())
						{
							EndServerBind();
							AbortTransport("UDP bind closed during promotion");
							return;
						}

						EndServerBind();
						m_serverBindResponseActive = true;
						m_bindDeadline_ns = NOW_NS() + kControlDeadlineNs;
						SendBindResponse();
						ScheduleServerBindResponseRetry();
					});
			}
			return;

		case eSystemPacketId::UDP_BIND_CONFIRM:
			if (!IsServerSide() || view.PayloadSize() < sizeof(UDP_BIND_CONFIRM_DATA))
				return;
			if (const auto* confirm = reinterpret_cast<const UDP_BIND_CONFIRM_DATA*>(view.Payload());
				confirm && confirm->sessionId == GetSessionId() && confirm->transactionId == m_bindTransactionId)
			{
				m_serverBindResponseActive = false;
				++m_bindTimerToken;
			}
			return;

		case eSystemPacketId::UDP_UNBIND_REQ:
			if (view.PayloadSize() < sizeof(UDP_UNBIND_REQ_DATA))
				return;
			if (const auto* req = reinterpret_cast<const UDP_UNBIND_REQ_DATA*>(view.Payload());
				!req || req->sessionId != GetSessionId() || req->transactionId == 0)
				return;
			else
			{
				m_unbindTransactionId = req->transactionId;
				SendUnbindResponse();
				if (!m_unbindTombstone)
				{
					m_unbindTombstone = true;
					m_unbindDeadline_ns = NOW_NS() + kControlDeadlineNs;
					NotifyLinkTerminatedIfEstablished();
					ScheduleUnbindTombstoneExpiry();
				}
			}
			return;

		case eSystemPacketId::UDP_UNBIND_RES:
			if (view.PayloadSize() < sizeof(UDP_UNBIND_RES_DATA))
				return;
			if (const auto* res = reinterpret_cast<const UDP_UNBIND_RES_DATA*>(view.Payload());
				res && m_unbindRequestActive && res->sessionId == GetSessionId() && res->transactionId == m_unbindTransactionId)
			{
				m_unbindRequestActive = false;
				++m_unbindTimerToken;
				CompleteProtocolDisconnect();
			}
			return;

		default: break;
		}
	}

	void UdpSession::OnPendingDispatchDrained()
	{
		if (!m_releaseQueued.exchange(false, std::memory_order_acq_rel))
			return;

		auto service = GetServiceRef();
		if (!service)
			return;

		const SessionId sessionId = GetSessionId();
		const EndpointHandle endpoint = GetEndpointHandle();
		const uint32 generation = GetServiceGeneration();
		Job release([service, sessionId, endpoint, generation]
			{
				auto* self = static_cast<UdpSession*>(service->FindOwnedSession(sessionId, endpoint, generation));
				if (self)
					service->ReleaseUdpSession(self);
			}, eJobPriority::Control);

		if (service->GetServiceType() == eServiceType::CLIENT)
			std::static_pointer_cast<ClientService>(service)->GetPrincipalShard()->Submit(std::move(release));
		else
			Submit(std::move(release));
	}

	void UdpSession::ProcessSystemPacket(Packet packet, const PacketHeaderView& view, uint64 ingressRecvTime_ns)
	{
		if (!view.IsValid() || view.Type() != ePacketType::SYSTEM)
			return;

		if (IsUdpControlPacket(view))
		{
			HandleUdpControlPacket(view);
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


	void UdpSession::TrySessionBinding()
	{
		if (!IsClientSide() || !IsConnected())
			return;
		if (m_clientBind.active || m_clientBind.bound)
			return;
		if (m_accountId == 0 || m_userId == kInvalidRuntimeId)
			return;

		m_clientBind.active		= true;
		m_clientBind.bound		= false;
		m_bindTransactionId = MakeTransactionId(*this);
		m_bindDeadline_ns = NOW_NS() + kControlDeadlineNs;
		SetSessionState(eSessionState::Binding);
		SendBindRequest();
		ScheduleSessionBindingRetry();
	}

	void UdpSession::ScheduleSessionBindingRetry()
	{
		const uint32 token = ++m_clientBind.timerToken;
		const EndpointHandle endpointHandle = GetEndpointHandle();
		const uint32 generation = GetServiceGeneration();
		auto service = GetServiceRef();
		SubmitAfter(Job([service, endpointHandle, generation, token]()
			{
				auto* self = service ? static_cast<UdpSession*>(service->FindOwnedSession(kInvalidSessionId, endpointHandle, generation)) : nullptr;
				if (!self)
					return;
				if (!self->m_clientBind.active || self->m_clientBind.bound || self->m_clientBind.timerToken != token)
					return;
				if (NOW_NS() >= self->m_bindDeadline_ns)
				{
					self->m_clientBind.active = false;
					self->m_clientBind.bound  = false;
					JAM_LOG_ERROR("[UdpSession] UDP bind timed out. accountId={}, userId={}, transactionId={}", self->m_accountId, self->m_userId, self->m_bindTransactionId);
					self->AbortTransport("UDP bind deadline exceeded");
					return;
				}

				self->SendBindRequest();
				self->ScheduleSessionBindingRetry();
			}, eJobPriority::Control), RetryDelayNs(m_endpointId, token));
	}

	void UdpSession::ScheduleServerBindResponseRetry()
	{
		const uint32 token = ++m_bindTimerToken;
		const SessionId sessionId = GetSessionId();
		const EndpointHandle endpoint = GetEndpointHandle();
		const uint32 generation = GetServiceGeneration();
		auto service = GetServiceRef();
		SubmitAfter(Job([service, sessionId, endpoint, generation, token]()
			{
				auto* self = service ? static_cast<UdpSession*>(service->FindOwnedSession(sessionId, endpoint, generation)) : nullptr;
				if (!self || !self->m_serverBindResponseActive || self->m_bindTimerToken != token)
					return;
				if (NOW_NS() >= self->m_bindDeadline_ns)
				{
					JAM_LOG_ERROR("[UdpSession] UDP bind confirm timed out. accountId={}, userId={}, sessionId={}", self->m_accountId, self->m_userId, self->m_sessionId);
					self->AbortTransport("UDP bind confirm deadline exceeded");
					return;
				}
				self->SendBindResponse();
				self->ScheduleServerBindResponseRetry();
			}, eJobPriority::Control), RetryDelayNs(m_endpointId, token));
	}

	void UdpSession::ScheduleSessionUnbindRetry()
	{
		const uint32 token = ++m_unbindTimerToken;
		const SessionId sessionId = GetSessionId();
		const EndpointHandle endpoint = GetEndpointHandle();
		const uint32 generation = GetServiceGeneration();
		auto service = GetServiceRef();
		SubmitAfter(Job([service, sessionId, endpoint, generation, token]()
			{
				auto* self = service ? static_cast<UdpSession*>(service->FindOwnedSession(sessionId, endpoint, generation)) : nullptr;
				if (!self || !self->m_unbindRequestActive || self->m_unbindTimerToken != token)
					return;
				if (NOW_NS() >= self->m_unbindDeadline_ns)
				{
					JAM_LOG_WARN("[UdpSession] UDP unbind timed out. sessionId={}", self->m_sessionId);
					self->AbortTransport("UDP unbind deadline exceeded");
					return;
				}
				self->SendUnbindRequest();
				self->ScheduleSessionUnbindRetry();
			}, eJobPriority::Control), RetryDelayNs(m_endpointId, token));
	}

	void UdpSession::ScheduleUnbindTombstoneExpiry()
	{
		const uint32 token = ++m_unbindTimerToken;
		const SessionId sessionId = GetSessionId();
		const EndpointHandle endpoint = GetEndpointHandle();
		const uint32 generation = GetServiceGeneration();
		auto service = GetServiceRef();
		SubmitAfter(Job([service, sessionId, endpoint, generation, token]()
			{
				auto* self = service ? static_cast<UdpSession*>(service->FindOwnedSession(sessionId, endpoint, generation)) : nullptr;
				if (!self || !self->m_unbindTombstone || self->m_unbindTimerToken != token)
					return;
				if (NOW_NS() < self->m_unbindDeadline_ns)
				{
					self->ScheduleUnbindTombstoneExpiry();
					return;
				}
				self->CompleteProtocolDisconnect();
			}, eJobPriority::Control), RetryDelayNs(m_endpointId, token));
	}

	void UdpSession::SendBindRequest()
	{
		const UDP_BIND_REQ_DATA data{ .accountId = m_accountId, .userId = m_userId, .transactionId = m_bindTransactionId };
		SendImmediatePacket(PacketBuilder::CreateSystemPacket(eSystemPacketId::UDP_BIND_REQ, PacketFlags::NONE, eChannel::UDP_DEFAULT, &data, sizeof(data)));
	}

	void UdpSession::SendBindResponse()
	{
		const bool success = GetSessionId() != kInvalidSessionId;
		const UDP_BIND_RES_DATA data{ .accountId = m_accountId, .userId = m_userId, .sessionId = success ? GetSessionId() : kInvalidSessionId,
			.transactionId = m_bindTransactionId, .success = static_cast<uint8>(success) };
		SendImmediatePacket(PacketBuilder::CreateSystemPacket(eSystemPacketId::UDP_BIND_RES, PacketFlags::NONE, eChannel::UDP_DEFAULT, &data, sizeof(data)));
	}

	void UdpSession::SendBindConfirm()
	{
		const UDP_BIND_CONFIRM_DATA data{ .sessionId = GetSessionId(), .transactionId = m_bindTransactionId };
		SendImmediatePacket(PacketBuilder::CreateSystemPacket(eSystemPacketId::UDP_BIND_CONFIRM, PacketFlags::NONE, eChannel::UDP_DEFAULT, &data, sizeof(data)));
	}

	void UdpSession::SendUnbindRequest()
	{
		const UDP_UNBIND_REQ_DATA data{ .sessionId = GetSessionId(), .transactionId = m_unbindTransactionId };
		SendImmediatePacket(PacketBuilder::CreateSystemPacket(eSystemPacketId::UDP_UNBIND_REQ, PacketFlags::NONE, eChannel::UDP_DEFAULT, &data, sizeof(data)));
	}

	void UdpSession::SendUnbindResponse()
	{
		const UDP_UNBIND_RES_DATA data{ .sessionId = GetSessionId(), .transactionId = m_unbindTransactionId };
		SendImmediatePacket(PacketBuilder::CreateSystemPacket(eSystemPacketId::UDP_UNBIND_RES, PacketFlags::NONE, eChannel::UDP_DEFAULT, &data, sizeof(data)));
	}

	void UdpSession::AbortTransport(const char* reason)
	{
		JAM_LOG_DEBUG("[UdpSession] transport aborted. reason={}, accountId={}, userId={}, sessionId={}", reason, m_accountId, m_userId, m_sessionId);
		m_clientBind.active = false;
		m_serverBindResponseActive = false;
		m_unbindRequestActive = false;
		++m_clientBind.timerToken;
		++m_bindTimerToken;
		++m_unbindTimerToken;
		CompleteProtocolDisconnect();
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
