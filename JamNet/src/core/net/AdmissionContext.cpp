#include "pch.h"
#include "jamnet/core/net/AdmissionContext.h"

#include "jambase/EnumUtils.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/memory/ObjectPool.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/SessionShardState.h"
#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpRouter.h"
#include "jamnet/core/net/UdpSession.h"
#include "jamnet/runtime/session/UserContext.h"


namespace jam::net
{
	namespace
	{
		inline constexpr uint32 kMaxAuthRetryCount	= 2;
		inline constexpr uint64 kAuthDeadlineNs		= 3_s;
		inline constexpr uint64 kAuthRetryDelayNs	= 100_ms;
	}


	bool AdmissionContext::SetAuthenticator(std::shared_ptr<IAuthenticator> authenticator)
	{
		if (!authenticator)
			return false;
		if (m_authenticator && m_authenticator != authenticator)
			return false;

		m_authenticator = std::move(authenticator);
		return true;
	}

	AdmissionKey AdmissionContext::AddEntry(std::unique_ptr<Session> session)
	{
		if (!session || session->GetProtocolType() == eProtocolType::NONE || session->GetEndpointId() == kInvalidEndpointId)
			return {};

		AdmissionKey key{
			.admissionId	= m_nextAdmissionId++,
			.protocol		= session->GetProtocolType(),
			.endpointId		= session->GetEndpointId(),
		};

		AdmissionEntry entry{
			.phase	= eAdmissionPhase::Active,
			.session	= std::move(session),
			.protocolData = key.protocol == eProtocolType::TCP
				? AdmissionProtocolData{ TcpAdmissionData{} }
				: AdmissionProtocolData{ UdpAdmissionData{} },
		};

		if (!m_entries.emplace(key, std::move(entry)).second)
			return {};

		return key;
	}

	void AdmissionContext::RequestTcpRelease(const AdmissionKey& key, Session* expected)
	{
		if (!m_iocpCore || !expected)
			return;

		auto* event = ObjectPool<AdmissionTcpReleaseEvent>::Pop();
		event->key = key;
		event->expected = expected;
		if (!m_iocpCore->Post(this, event))
			ObjectPool<AdmissionTcpReleaseEvent>::Push(event);
	}

	void AdmissionContext::OnTcpPacket(const AdmissionKey& key, Packet packet)
	{
		AdmissionEntry* entry = FindAdmissionEntry(key);
		if (!entry || entry->phase != eAdmissionPhase::Active || !entry->session || !entry->session->IsTcp() || !packet.IsValid())
			return;

		auto* tcpData = std::get_if<TcpAdmissionData>(&entry->protocolData);
		if (!tcpData || tcpData->state != eTcpAdmissionState::AwaitingBind)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid() || view.TotalSize() != packet->Size() || view.Type() != ePacketType::SYSTEM
			|| ToEnum<eSystemPacketId>(view.Id()) != eSystemPacketId::TCP_BIND_REQ
			|| view.PayloadSize() < sizeof(TCP_BIND_REQ_DATA))
		{
			return;
		}

		const auto* request = reinterpret_cast<const TCP_BIND_REQ_DATA*>(view.Payload());
		if (!request || request->field0Size > kMaxAuthFieldBytes || request->field1Size > kMaxAuthFieldBytes)
			return;

		AuthCredential credential{
			.scheme = request->scheme,
		};
		credential.field0.size = request->field0Size;
		credential.field1.size = request->field1Size;
		std::memcpy(credential.field0.bytes.data(), request->field0, request->field0Size);
		std::memcpy(credential.field1.bytes.data(), request->field1, request->field1Size);

		tcpData->authCredential = credential;
		tcpData->authRetryCount = 0;
		tcpData->authDeadlineNs = NOW_NS() + kAuthDeadlineNs;
		tcpData->state			= eTcpAdmissionState::Authenticating;

		SubmitAuthentication(key, credential);
		ScheduleAuthTimeout(key);
	}

	void AdmissionContext::OnUdpPacket(const AdmissionKey& key, Packet packet)
	{
		AdmissionEntry* entry = FindAdmissionEntry(key);
		if (!entry || entry->phase != eAdmissionPhase::Active || !entry->session || !entry->session->IsUdp() || !packet.IsValid())
		{
			return;
		}

		auto* udp = std::get_if<UdpAdmissionData>(&entry->protocolData);
		if (!udp)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid() || view.TotalSize() != packet->Size() || view.Type() != ePacketType::SYSTEM
			|| ToEnum<eSystemPacketId>(view.Id()) != eSystemPacketId::UDP_BIND_REQ
			|| view.PayloadSize() < sizeof(UDP_BIND_REQ_DATA))
		{
			return;
		}

		const auto* request = reinterpret_cast<const UDP_BIND_REQ_DATA*>(view.Payload());
		if (!request || request->accountId == 0 || request->userId == kInvalidRuntimeId || request->transactionId == 0)
			return;

		if (udp->transactionId != 0)
		{
			if (udp->transactionId != request->transactionId)
				return;
		}
		else
		{
			udp->transactionId = request->transactionId;
		}

		PromoteUdpToShard(key, *entry, request->accountId, request->userId, request->transactionId);
	}

	void AdmissionContext::Dispatch(IocpEvent* event, int32 bytes)
	{
		(void)bytes;
		if (!event)
			return;

		switch (event->m_eventType)
		{
		case eEventType::AdmissionAuthCompleted:
		{
			auto* completed = static_cast<AuthCompletedEvent*>(event);
			OnAuthCompleted(*completed);
			ObjectPool<AuthCompletedEvent>::Push(completed);
			break;
		}

		case eEventType::AdmissionAuthRetry:
		{
			auto* retry = static_cast<AuthRetryEvent*>(event);
			OnAuthRetry(*retry);
			ObjectPool<AuthRetryEvent>::Push(retry);
			break;
		}

		case eEventType::AdmissionAuthTimeout:
		{
			auto* timeout = static_cast<AuthTimeoutEvent*>(event);
			OnAuthTimeout(*timeout);
			ObjectPool<AuthTimeoutEvent>::Push(timeout);
			break;
		}

		case eEventType::AdmissionTcpRelease:
		{
			auto* release = static_cast<AdmissionTcpReleaseEvent*>(event);
			ReleaseTcpEntry(*release);
			ObjectPool<AdmissionTcpReleaseEvent>::Push(release);
			break;
		}

		default:
			break;
		}
	}



	void AdmissionContext::SubmitAuthentication(const AdmissionKey& key, AuthCredential credential)
	{
		if (!m_authenticator)
		{
			if (AdmissionEntry* entry = FindAdmissionEntry(key))
				RejectTcp(*entry);
			return;
		}

		auto authenticator = m_authenticator;
		std::weak_ptr<AdmissionContext> weakContext = std::static_pointer_cast<AdmissionContext>(shared_from_this());

		GLOBAL_EXEC.Submit(Job([weakContext, authenticator = std::move(authenticator), key, credential = credential]() mutable
			{
				authenticator->Authenticate(credential,
				                            [weakContext, key](const AuthResult& result) mutable
				                            {
					                            if (auto context = weakContext.lock())
						                            context->PostAuthCompleted(key, result);
				                            });
			}));
	}

	void AdmissionContext::PostAuthCompleted(const AdmissionKey& key, const AuthResult& result)
	{
		if (!m_iocpCore)
			return;

		auto* event = ObjectPool<AuthCompletedEvent>::Pop();
		event->key    = key;
		event->result = result;

		if (!m_iocpCore->Post(this, event))
			ObjectPool<AuthCompletedEvent>::Push(event);
	}

	void AdmissionContext::PostAuthRetry(const AdmissionKey& key)
	{
		if (!m_iocpCore)
			return;

		auto* event = ObjectPool<AuthRetryEvent>::Pop();
		event->key = key;
		if (!m_iocpCore->Post(this, event))
			ObjectPool<AuthRetryEvent>::Push(event);
	}

	void AdmissionContext::PostAuthTimeout(const AdmissionKey& key)
	{
		if (!m_iocpCore)
			return;

		auto* event = ObjectPool<AuthTimeoutEvent>::Pop();
		event->key = key;
		if (!m_iocpCore->Post(this, event))
			ObjectPool<AuthTimeoutEvent>::Push(event);
	}


	void AdmissionContext::ScheduleAuthRetry(const AdmissionKey& key, TcpAdmissionData& tcp)
	{
		++tcp.authRetryCount;
		tcp.state = eTcpAdmissionState::AuthRetryWaiting;

		std::weak_ptr<AdmissionContext> weakContext = std::static_pointer_cast<AdmissionContext>(shared_from_this());
		const uint64 delayNs = kAuthRetryDelayNs * tcp.authRetryCount;
		GLOBAL_EXEC.SubmitAfter(Job([weakContext, key]
			{
				if (auto context = weakContext.lock())
					context->PostAuthRetry(key);
			}), delayNs);
	}

	void AdmissionContext::ScheduleAuthTimeout(const AdmissionKey& key)
	{
		std::weak_ptr<AdmissionContext> weakContext = std::static_pointer_cast<AdmissionContext>(shared_from_this());
		GLOBAL_EXEC.SubmitAfter(Job([weakContext, key]
			{
				if (auto context = weakContext.lock())
					context->PostAuthTimeout(key);
			}), kAuthDeadlineNs);
	}


	void AdmissionContext::OnAuthCompleted(const AuthCompletedEvent& event)
	{
		AdmissionEntry* entry = FindAdmissionEntry(event.key);
		if (!entry || entry->phase != eAdmissionPhase::Active || !entry->session || !entry->session->IsTcp())
			return;

		auto* tcp = std::get_if<TcpAdmissionData>(&entry->protocolData);
		if (!tcp || tcp->state != eTcpAdmissionState::Authenticating)
			return;

		if (event.result.success && event.result.principalId != 0)
		{
			tcp->authCredential.reset();
			entry->session->SetAccountId(event.result.principalId);
			PromoteTcpToShard(event.key, *entry, event.result.principalId);
			return;
		}

		if (event.result.retryable && tcp->authCredential && tcp->authRetryCount < kMaxAuthRetryCount && NOW_NS() < tcp->authDeadlineNs)
		{
			ScheduleAuthRetry(event.key, *tcp);
			return;
		}

		RejectTcp(*entry);
	}

	void AdmissionContext::OnAuthRetry(const AuthRetryEvent& event)
	{
		AdmissionEntry* entry = FindAdmissionEntry(event.key);
		if (!entry || entry->phase != eAdmissionPhase::Active || !entry->session || !entry->session->IsTcp())
			return;

		auto* tcp = std::get_if<TcpAdmissionData>(&entry->protocolData);
		if (!tcp || tcp->state != eTcpAdmissionState::AuthRetryWaiting || !tcp->authCredential)
			return;

		if (entry->session->IsClosing() || NOW_NS() >= tcp->authDeadlineNs)
		{
			RejectTcp(*entry);
			return;
		}

		tcp->state = eTcpAdmissionState::Authenticating;
		SubmitAuthentication(event.key, *tcp->authCredential);
	}

	void AdmissionContext::OnAuthTimeout(const AuthTimeoutEvent& event)
	{
		AdmissionEntry* entry = FindAdmissionEntry(event.key);
		if (!entry || entry->phase != eAdmissionPhase::Active || !entry->session || !entry->session->IsTcp())
			return;

		auto* tcp = std::get_if<TcpAdmissionData>(&entry->protocolData);
		if (!tcp || (tcp->state != eTcpAdmissionState::Authenticating && tcp->state != eTcpAdmissionState::AuthRetryWaiting))
		{
			return;
		}

		if (NOW_NS() >= tcp->authDeadlineNs)
			RejectTcp(*entry);
	}

	void AdmissionContext::RejectTcp(AdmissionEntry& entry)
	{
		if (entry.phase != eAdmissionPhase::Active || !entry.session || !entry.session->IsTcp())
			return;

		if (auto* data = std::get_if<TcpAdmissionData>(&entry.protocolData))
			data->authCredential.reset();

		entry.phase = eAdmissionPhase::Closing;

		auto* tcp = static_cast<TcpSession*>(entry.session.get());
		constexpr TCP_BIND_RES_DATA response{
			.accountId		= 0,
			.userId			= kInvalidRuntimeId,
			.sessionId		= kInvalidSessionId,
			.success		= 0,
			.bootstrapKind	= eBootstrapKind::Pending,
		};

		tcp->SendImmediatePacket(PacketBuilder::CreateTcpBindResPacket(response));
		tcp->Disconnect();
	}

	void AdmissionContext::ReleaseTcpEntry(const AdmissionTcpReleaseEvent& event)
	{
		auto it = m_entries.find(event.key);
		if (it == m_entries.end() || !it->second.session || it->second.session.get() != event.expected)
			return;

		std::unique_ptr<Session> owner = std::move(it->second.session);
		m_entries.erase(it);

		if (auto* tcp = static_cast<TcpSession*>(owner.get()))
			tcp->ClearAdmissionContext();

		auto service = owner->GetServiceRef();
		owner->SetService(nullptr);
		owner->CompleteClose();
		if (service)
			service->NotifyTcpSessionDetached();
	}

	void AdmissionContext::PromoteTcpToShard(const AdmissionKey& key, AdmissionEntry& entry, uint64 principalId)
	{
		if (entry.phase != eAdmissionPhase::Active || !entry.session || principalId == 0)
			return;

		auto it = m_entries.find(key);
		if (it == m_entries.end() || &it->second != &entry)
			return;

		const RouteKey ownerRouteKey = GLOBAL_EXEC.MakeAffinityRouteKey(principalId);
		auto ownerShard = GLOBAL_EXEC.GetShard(ownerRouteKey);
		if (!ownerShard)
		{
			entry.phase = eAdmissionPhase::Closing;
			return;
		}

		entry.phase = eAdmissionPhase::Promoting;
		std::unique_ptr<Session> owner = std::move(entry.session);
		m_entries.erase(it);

		auto* tcp = static_cast<TcpSession*>(owner.get());
		tcp->ClearAdmissionContext();
		owner->m_key   = ownerRouteKey;
		owner->m_shard = ownerShard;

		ownerShard->Submit(Job([owner = std::move(owner), ownerShard, principalId]() mutable
			{
				auto* tcp = static_cast<TcpSession*>(owner.get());
				if (!tcp)
					return;

				auto& state = GetOrCreateSessionShardState(CurrentShardLocalChecked());
				const SessionId sessionId = state.AdoptSession(owner);
				if (sessionId == kInvalidSessionId)
					return;

				tcp->m_sessionId = sessionId;
				tcp->m_mailboxRef = ownerShard->CreateMailboxRef(sessionId);

				if (tcp->IsClosing())
				{
					if (auto service = tcp->GetServiceRef())
						service->ReleaseTcpSession(tcp);
					return;
				}

				auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
				UserContext* user = userState.EnsureUserContext(principalId);
				if (!user)
				{
					tcp->Disconnect();
					return;
				}
				const UserId userId = user->userId;
				const eBootstrapKind bootstrapKind = user->worldState.main ? eBootstrapKind::Resync : eBootstrapKind::Fresh;

				tcp->SetAccountId(principalId);
				tcp->SetUserId(userId);
				if (tcp->GetEntity() == entt::null)
					tcp->CreateEntity();
				tcp->CompleteSessionEstablishment(true);

				const TCP_BIND_RES_DATA response{
					.accountId		= principalId,
					.userId			= userId,
					.sessionId		= sessionId,
					.success		= 1,
					.bootstrapKind	= bootstrapKind,
				};

				tcp->SendImmediatePacket(PacketBuilder::CreateTcpBindResPacket(response));
			}, eJobPriority::Critical));
	}

	void AdmissionContext::PromoteUdpToShard(const AdmissionKey& key, AdmissionEntry& entry, uint64 accountId, RuntimeId userId, uint64 transactionId)
	{
		if (entry.phase != eAdmissionPhase::Active || !entry.session || !entry.session->IsUdp()
			|| accountId == 0 || userId == kInvalidRuntimeId || transactionId == 0)
			return;

		auto it = m_entries.find(key);
		if (it == m_entries.end() || &it->second != &entry)
			return;

		const RouteKey ownerRouteKey = GLOBAL_EXEC.MakeAffinityRouteKey(accountId);
		auto ownerShard = GLOBAL_EXEC.GetShard(ownerRouteKey);
		if (!ownerShard)
			return;

		entry.phase = eAdmissionPhase::Promoting;
		std::unique_ptr<Session> owner = std::move(entry.session);
		m_entries.erase(it);

		owner->m_key = ownerRouteKey;
		owner->m_shard = ownerShard;
		ownerShard->Submit(Job([owner = std::move(owner), ownerShard, key, accountId, userId, transactionId]() mutable
			{
				auto* udp = static_cast<UdpSession*>(owner.get());
				if (!udp)
					return;

				auto service = udp->GetServiceRef();
				auto reject = [&]() mutable
					{
						if (service && service->m_udpRouter)
							service->m_udpRouter->ClearIngressAdmission(key.endpointId, key.admissionId);
						udp->m_bindTransactionId = transactionId;
						udp->SendBindResponse();
						udp->SetService(nullptr);
						udp->CompleteClose();
						if (service)
						{
							service->m_udpSessionCount.fetch_sub(1, std::memory_order_relaxed);
							service->m_sessionCount.fetch_sub(1, std::memory_order_relaxed);
						}
					};

				auto& userState = GetOrCreateUserShardState(CurrentShardLocalChecked());
				const UserContext* user = userState.FindUserContext(userId);
				if (!user || user->accountId != accountId || user->tcp == kInvalidSessionId || udp->IsClosing())
				{
					reject();
					return;
				}

				auto& state = GetOrCreateSessionShardState(CurrentShardLocalChecked());
				const SessionId sessionId = state.AdoptSession(owner);
				if (sessionId == kInvalidSessionId)
				{
					reject();
					return;
				}

				udp->m_sessionId = sessionId;
				udp->m_mailboxRef = ownerShard->CreateMailboxRef(sessionId);
				if (!service || !service->m_udpRouter || !service->m_udpRouter->PromoteIngressToBound(key.endpointId, key.admissionId, sessionId))
				{
					owner = state.ReleaseSession(sessionId, udp);
					udp = static_cast<UdpSession*>(owner.get());
					if (udp)
					{
						if (udp->m_mailboxRef.mailbox)
							ownerShard->CloseMailbox(udp->m_mailboxRef.mailbox->GetId(), eMailboxCloseMode::Abort);
						udp->m_mailboxRef = {};
						udp->m_sessionId = kInvalidSessionId;
						reject();
					}
					return;
				}

				udp->m_bindTransactionId = transactionId;
				udp->SetAccountId(accountId);
				udp->SetUserId(userId);
				udp->m_bindAccepted = true;
				if (udp->GetEntity() == entt::null)
					udp->CreateEntity();
				udp->CompleteSessionEstablishment(true);
				udp->SendBindResponse();
			}, eJobPriority::Critical));
	}


	AdmissionEntry* AdmissionContext::FindAdmissionEntry(const AdmissionKey& key)
	{
		if (auto it = m_entries.find(key); it != m_entries.end())
			return &it->second;

		return nullptr;
	}
}
