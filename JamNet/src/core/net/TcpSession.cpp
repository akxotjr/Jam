#include "pch.h"
#include "jamnet/core/net/TcpSession.h"

#include "jambase/EnumUtils.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/memory/ObjectPool.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/utils/Clock.h"
#include "jamnet/core/net/WinErrorHandling.h"

#include <limits>

namespace jam::net
{

	namespace
	{
		inline constexpr uint64 kClientBindRetryDelayNs = 1000_ms;

		RouteKey MakeBoundSessionRouteKey(uint64 accountId)
		{
			return GLOBAL_EXEC.MakeAffinityRouteKey(accountId);
		}

		void SendTcpBindResponse(Session& session, bool success, uint64 accountId, RuntimeId userId,
			eBootstrapKind bootstrapKind = eBootstrapKind::Pending)
		{
			const SessionId sessionId = success ? session.GetSessionId() : kInvalidSessionId;
			const TCP_BIND_RES_DATA res
			{
				.accountId = accountId,
				.userId = userId,
				.sessionId = sessionId,
				.success = static_cast<uint8>(success ? 1 : 0),
				.bootstrapKind = success ? bootstrapKind : eBootstrapKind::Pending,
			};
			auto pkt = PacketBuilder::CreateSystemPacket(eSystemPacketId::TCP_BIND_RES, PacketFlags::NONE, eChannel::TCP_DEFAULT, &res, sizeof(res));
			session.Send(pkt);
		}

		bool IsTcpBindPacket(const PacketHeaderView& view)
		{
			if (!view.IsValid() || view.Type() != ePacketType::SYSTEM)
				return false;

			const auto id = ToEnum<eSystemPacketId>(view.Id());
			return id == eSystemPacketId::TCP_BIND_REQ || id == eSystemPacketId::TCP_BIND_RES;
		}

	}


	TcpSession::TcpSession() 
		: m_recvAssembler(GetNetBufferPool(eNetBufferPoolKind::TcpIo))
	{
		m_protocol = eProtocolType::TCP;
		m_socket   = SocketUtils::CreateSocket(eProtocolType::TCP);
	}

	TcpSession::~TcpSession()
	{
		SocketUtils::Close(m_socket);
	}

	void TcpSession::SetPasswordCredential(std::string loginId, std::string password)
	{
		m_loginRequest = {};
		m_loginRequest.kind = eLoginCredentialKind::Password;
		m_loginRequest.loginIdSize = static_cast<uint16>(std::min(loginId.size(), kMaxLoginIdBytes));
		m_loginRequest.secretSize = static_cast<uint16>(std::min(password.size(), kMaxLoginSecretBytes));
		std::memcpy(m_loginRequest.loginId, loginId.data(), m_loginRequest.loginIdSize);
		std::memcpy(m_loginRequest.secret, password.data(), m_loginRequest.secretSize);
	}

	void TcpSession::SetTicketCredential(std::vector<uint8> ticket)
	{
		m_loginRequest = {};
		m_loginRequest.kind = eLoginCredentialKind::Ticket;
		m_loginRequest.secretSize = static_cast<uint16>(std::min(ticket.size(), kMaxLoginSecretBytes));
		std::memcpy(m_loginRequest.secret, ticket.data(), m_loginRequest.secretSize);
	}

	bool TcpSession::Connect()
	{
		if (!GetService())
		{
			JAM_LOG_ERROR("[TcpSession] Cannot connect without a service");
			return false;
		}
		if (!GetService()->RegisterIocpObject(this))
		{
			JAM_LOG_ERROR("[TcpSession] Failed to register socket with IOCP");
			return false;
		}

		return RegisterConnect();
	}

	void TcpSession::Disconnect()
	{
		if (IsClosing())
			return;

		const bool posted = RegisterDisconnect();
		MarkClosing();

		if (!posted)
		{
			SocketUtils::Close(m_socket);
			m_releaseQueued.store(true, std::memory_order_release);
			if (GetPendingDispatchCount() == 0)
				OnPendingDispatchDrained();
		}
	}

	void TcpSession::Send(Packet packet)
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (!packet.IsValid())
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (view.IsValid() && view.TotalSize() == packet->Size() && view.Type() == ePacketType::SYSTEM
			&& (IsTcpBindPacket(view) || GetEntity() == entt::null || !CanCreateSessionEntity()))
		{
			SendImmediatePacket(packet);
			return;
		}

		const entt::entity e = GetEntity();
		if (e == entt::null)
			return;

		SendPacketToSession(e, std::move(packet));
	}

	HANDLE TcpSession::GetHandle()
	{
		return reinterpret_cast<HANDLE>(m_socket);
	}

	void TcpSession::Dispatch(IocpEvent* iocpEvent, int32 bytes)
	{
		switch (iocpEvent->m_eventType)
		{
		case eEventType::TcpConnect:
			ProcessOutboundConnect();
			break;

		case eEventType::TcpDisconnect:
			ProcessDisconnect();
			break;

		case eEventType::TcpSend:
			ProcessSend(static_cast<TcpSendEvent*>(iocpEvent), bytes);
			break;

		case eEventType::TcpRecv:
			ProcessRecv(static_cast<TcpRecvEvent*>(iocpEvent), bytes);
			break;

		default:break;
		}
	}

	bool TcpSession::RegisterConnect()
	{
		if (SocketUtils::SetReuseAddress(m_socket, true) == false)
		{
			win_error::LogLastWsaError("[TcpSession] SO_REUSEADDR before ConnectEx");
			return false;
		}
		if (SocketUtils::BindAnyAddress(m_socket, 0) == false)
		{
			win_error::LogLastWsaError("[TcpSession] bind before ConnectEx");
			return false;
		}

		m_connectEvent.Init();
		if (!TryAddPendingDispatch())
		{
			JAM_LOG_ERROR("[TcpSession] Failed to reserve ConnectEx dispatch");
			return false;
		}

		DWORD		bytes	 = 0;
		SOCKADDR_IN sockAddr = GetService()->GetRemoteTcpNetAddress().GetSockAddr();

		if (SOCKET_ERROR == SocketUtils::ConnectEx(m_socket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr), nullptr, 0, &bytes, &m_connectEvent))
		{
			const int32 errorCode = win_error::GetLastWsaError();
			if (!win_error::IsIoPending(errorCode))
			{
				ReleasePendingDispatch();
				win_error::LogWsaError("[TcpSession] ConnectEx", errorCode);
				return false;
			}
		}
		return true;
	}

	bool TcpSession::RegisterDisconnect()
	{
		m_disconnectEvent.Init();
		if (!TryAddPendingDispatch())
			return false;

		if (false == SocketUtils::DisconnectEx(m_socket, &m_disconnectEvent, TF_REUSE_SOCKET, 0))
		{
			const int32 errorCode = win_error::GetLastWsaError();
			if (!win_error::IsIoPending(errorCode))
			{
				ReleasePendingDispatch();
				return false;
			}
		}
		return true;
	}

	void TcpSession::RegisterSend(std::vector<PacketChain>&& chains)
	{
		if (IsClosing())
			return;

		auto* ev = ObjectPool<TcpSendEvent>::Pop();
		ev->Init();
		ev->chains     = std::move(chains);
		ev->curIndex   = 0;
		ev->curOffset  = 0;
		ev->totalBytes = 0;
		ev->wsaBufs.clear();
		size_t partCount = 0;
		for (const auto& chain : ev->chains)
			partCount += chain.Count();
		ev->wsaBufs.reserve(partCount);		// chain 수가 들쭉날쭉한경우 allocate 비용이 발생할수 있음. 고정으로 하는게 좋아보임

		for (auto& chain : ev->chains)
		{
			for (const BufferSlice& part : chain.Parts())
			{
				if (!part.IsValid() || part.Size() == 0)
					continue;

				WSABUF w;
				w.buf = reinterpret_cast<char*>(part.Head());
				w.len = static_cast<ULONG>(part.Size());

				ev->wsaBufs.push_back(w);
				ev->totalBytes += w.len;
			}
		}

		if (ev->wsaBufs.empty())
		{
			ev->chains.clear();
			ObjectPool<TcpSendEvent>::Push(ev);
			return;
		}

		if (!TryAddPendingDispatch())
		{
			ev->chains.clear();
			ev->wsaBufs.clear();
			ObjectPool<TcpSendEvent>::Push(ev);
			return;
		}

		DWORD sent = 0;
		if (SOCKET_ERROR == ::WSASend(m_socket, ev->wsaBufs.data(), static_cast<DWORD>(ev->wsaBufs.size()), OUT & sent, 0, ev, nullptr))
		{
			const int32 ec = win_error::GetLastWsaError();
			if (!win_error::IsIoPending(ec))
			{
				ReleasePendingDispatch();
				HandleError(ec);
				ev->chains.clear();
				ev->wsaBufs.clear();
				ObjectPool<TcpSendEvent>::Push(ev);
			}
		}
	}


	void TcpSession::RegisterRecv()
	{
		if (IsClosing())
			return;

		auto* ev = ObjectPool<TcpRecvEvent>::Pop();
		ev->Init();
		ev->wsaBuf.buf = reinterpret_cast<CHAR*>(ev->buffer.data());
		ev->wsaBuf.len = static_cast<ULONG>(ev->buffer.size());
		if (!TryAddPendingDispatch())
		{
			ObjectPool<TcpRecvEvent>::Push(ev);
			return;
		}

		DWORD bytes = 0;
		DWORD flags = 0;
		if (SOCKET_ERROR == ::WSARecv(m_socket, &ev->wsaBuf, 1, OUT &bytes, OUT &flags, ev, nullptr))
		{
			const int32 errorCode = win_error::GetLastWsaError();
			if (!win_error::IsIoPending(errorCode))
			{
				ReleasePendingDispatch();
				HandleError(errorCode);
				ObjectPool<TcpRecvEvent>::Push(ev);
			}
		}
	}


	void TcpSession::ProcessInboundConnect()
	{
		if (!m_service->RegisterIocpObject(this))
		{
			SocketUtils::Close(m_socket);
			return;
		}

		SetSessionState(eSessionState::Connected);

		if (!IsClosing())
			RegisterRecv();
	}

	void TcpSession::ProcessOutboundConnect()
	{
		const ULONG_PTR nativeStatus = win_error::GetOverlappedNativeStatus(m_connectEvent);
		if (!win_error::IsNativeStatusSuccess(nativeStatus))
		{
			win_error::LogNativeStatus("[TcpSession] ConnectEx completion", nativeStatus);
			Disconnect();
			return;
		}

		if (!SocketUtils::SetUpdateConnectSocket(m_socket))
		{
			win_error::LogLastWsaError("[TcpSession] SO_UPDATE_CONNECT_CONTEXT");
			Disconnect();
			return;
		}

		SetSessionState(eSessionState::Connected);

		if (!IsClosing())
			RegisterRecv();

		TrySessionBinding();
	}

	void TcpSession::ProcessDisconnect()
	{
		SetSessionState(eSessionState::Disconnected);
		m_clientBind.active = false;
		++m_clientBind.timerToken;
		NotifyLinkTerminatedIfEstablished();

		m_releaseQueued.store(true, std::memory_order_release);
	}

	void TcpSession::OnPendingDispatchDrained()
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
				auto* self = static_cast<TcpSession*>(service->FindOwnedSession(sessionId, endpoint, generation));
				if (!self)
					return;

				self->NotifyDisconnectedOnce();
				service->ReleaseTcpSession(self);
			}, eJobPriority::Control);

		if (service->GetServiceType() == eServiceType::CLIENT)
			std::static_pointer_cast<ClientService>(service)->GetPrincipalShard()->Submit(std::move(release));
		else
			Submit(std::move(release));
	}

	void TcpSession::ProcessRecv(TcpRecvEvent* ev, int32 bytes)
	{
		if (!ev) return;

		if (bytes == 0)
		{
			ObjectPool<TcpRecvEvent>::Push(ev);
			Disconnect();
			return;
		}

		if (!m_recvAssembler.Append(ev->buffer.data(), bytes))
		{
			JAM_LOG_ERROR("TcpRecvAssembler::Append failed");
			ObjectPool<TcpRecvEvent>::Push(ev);
			Disconnect();
			return;
		}

		for (;;)
		{
			Packet pkt;
			const auto result = m_recvAssembler.TryExtractPacket(pkt);

			if (result == TcpRecvAssembler::eAssembleResult::NeedMoreData)
				break;

			if (result == TcpRecvAssembler::eAssembleResult::ProtocolError)
			{
				JAM_LOG_ERROR("Tcp recv protocol error");
				ObjectPool<TcpRecvEvent>::Push(ev);
				Disconnect();
				return;
			}

			const EndpointHandle endpointHandle = GetEndpointHandle();
			const SessionId sessionId = GetSessionId();
			const uint32 generation = GetServiceGeneration();
			auto service = GetServiceRef();
			Post(Job([service, endpointHandle, sessionId, generation, packet = std::move(pkt)]() mutable
				{
					auto* self = service ? static_cast<TcpSession*>(service->FindOwnedSession(sessionId, endpointHandle, generation)) : nullptr;
					if (!self) return;

					PacketHeaderView directView = PacketHeaderView::Parse(packet->Head(), packet->Size());
					if (directView.IsValid() && directView.TotalSize() == packet->Size() && directView.Type() == ePacketType::SYSTEM)
					{
						self->ProcessSystemPacket(std::move(packet), directView, 0_ns);
						return;
					}

					const entt::entity e = self->GetEntity();
					if (e != entt::null)
						ProcessReceivedPacket(e, std::move(packet));
				}, eJobPriority::Control));
		}

		ObjectPool<TcpRecvEvent>::Push(ev);
		if (!IsClosing())
			RegisterRecv();
	}


	void TcpSession::ProcessSend(TcpSendEvent* ev,  int32 bytes)
	{
		if (!ev) return;

		if (bytes <= 0)
		{
			ev->chains.clear();
			ev->wsaBufs.clear();
			ObjectPool<TcpSendEvent>::Push(ev);
			Disconnect();
			JAM_LOG_WARN_LOC("[TcpSession] Send 0 byte");
			return;
		}

		uint32 remaining = 0;
		if (bytes < static_cast<int32>(ev->totalBytes))
			remaining = ev->totalBytes - static_cast<uint32>(bytes);

		if (remaining == 0)
		{
			ev->chains.clear();
			ev->wsaBufs.clear();
			ObjectPool<TcpSendEvent>::Push(ev);
			return;
		}

		if (IsClosing())
		{
			ev->chains.clear();
			ev->wsaBufs.clear();
			ObjectPool<TcpSendEvent>::Push(ev);
			return;
		}

		uint32 advance = static_cast<uint32>(bytes);
		size_t idx	   = ev->curIndex;
		ULONG  off	   = ev->curOffset;

		while (advance > 0 && idx < ev->wsaBufs.size())
		{
			const ULONG leftInBuf = ev->wsaBufs[idx].len - off;
			if (advance < leftInBuf)
			{
				off = off + advance;
				advance = 0;
				break;
			}
			else
			{
				advance -= leftInBuf;
				idx++;
				off = 0;
			}
		}
		ev->curIndex   = idx;
		ev->curOffset  = off;
		ev->totalBytes = remaining;

		std::vector<WSABUF> bufs;
		if (ev->curIndex < ev->wsaBufs.size())
		{
			WSABUF first = ev->wsaBufs[ev->curIndex];
			first.buf += ev->curOffset;
			first.len -= ev->curOffset;

			bufs.reserve(ev->wsaBufs.size() - ev->curIndex);
			bufs.push_back(first);
			for (size_t i = ev->curIndex + 1; i < ev->wsaBufs.size(); ++i)
				bufs.push_back(ev->wsaBufs[i]);
		}

		if (bufs.empty())
		{
			ev->chains.clear();
			ev->wsaBufs.clear();
			ObjectPool<TcpSendEvent>::Push(ev);
			return;
		}

		if (!TryAddPendingDispatch())
		{
			ev->chains.clear();
			ev->wsaBufs.clear();
			ObjectPool<TcpSendEvent>::Push(ev);
			return;
		}

		DWORD sent = 0;
		if (SOCKET_ERROR == ::WSASend(m_socket, bufs.data(), static_cast<DWORD>(bufs.size()), OUT & sent, 0, ev, nullptr))
		{
			const int32 ec = win_error::GetLastWsaError();
			if (!win_error::IsIoPending(ec))
			{
				ReleasePendingDispatch();
				HandleError(ec);
				ev->chains.clear();
				ev->wsaBufs.clear();
				ObjectPool<TcpSendEvent>::Push(ev);
			}
		}
	}


	void TcpSession::HandleError(int32 errorCode)
	{
		switch (errorCode)
		{
		case WSAECONNRESET:
		case WSAECONNABORTED:
			Disconnect();
			break;
		default:
			win_error::LogWsaError("[TcpSession] socket operation", errorCode);
			break;
		}
	}

	void TcpSession::ProcessSystemPacket(Packet packet, const PacketHeaderView& view, uint64 ingressRecvTime_ns)
	{
		if (!view.IsValid() || view.Type() != ePacketType::SYSTEM)
			return;

		if (IsPreBindPhase())
		{
			HandlePreBindSystemPacket(view);
			return;
		}

		if (IsServerSide() && ToEnum<eSystemPacketId>(view.Id()) == eSystemPacketId::TCP_BIND_REQ)
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

	void TcpSession::HandlePreBindSystemPacket(const PacketHeaderView& view)
	{
		switch (ToEnum<eSystemPacketId>(view.Id()))
		{
		case eSystemPacketId::TCP_BIND_RES:
			if (!IsClientSide())
				return;
			if (view.PayloadSize() < sizeof(TCP_BIND_RES_DATA))
				return;
			if (m_clientBind.bound)
				return;

			if (const auto* res = reinterpret_cast<const TCP_BIND_RES_DATA*>(view.Payload()); res)
			{
				m_clientBind.active = false;
				++m_clientBind.timerToken;
				if (!res->success || res->userId == 0 || res->sessionId == kInvalidSessionId)
				{
					m_clientBind.active = false;
					m_clientBind.bound  = false;
					JAM_LOG_ERROR("[TcpSession] TCP bind failed or timed out. accountId={}", m_accountId);
					Disconnect();
					return;
				}
				if (res->bootstrapKind != eBootstrapKind::Fresh && res->bootstrapKind != eBootstrapKind::Resync)
				{
					JAM_LOG_ERROR("[TcpSession] TCP bind response has invalid bootstrap kind. accountId={}, kind={}", m_accountId, static_cast<uint32>(res->bootstrapKind));
					Disconnect();
					return;
				}

				m_accountId = res->accountId;
				m_userId = res->userId;
				m_clientBind.bound = true;
				if (!AdoptAuthoritativeSessionId(res->sessionId))
				{
					JAM_LOG_ERROR("[TcpSession] Failed to adopt authoritative session id. accountId={}, userId={}, sessionId={}", m_accountId, m_userId, res->sessionId);
					m_clientBind.active = false;
					m_clientBind.bound  = false;
					Disconnect();
					return;
				}
				OnTcpBindBootstrap(res->bootstrapKind);
			}
			break;

		case eSystemPacketId::TCP_BIND_REQ:
			if (!IsServerSide())
				return;
			if (view.PayloadSize() < sizeof(TCP_BIND_REQ_DATA))
				return;
			if (const auto* req = reinterpret_cast<const TCP_BIND_REQ_DATA*>(view.Payload()); !req
				|| req->loginIdSize > kMaxLoginIdBytes || req->secretSize == 0 || req->secretSize > kMaxLoginSecretBytes
				|| (req->kind != eLoginCredentialKind::Password && req->kind != eLoginCredentialKind::Ticket))
			{
				SendTcpBindResponse(*this, false, 0, kInvalidRuntimeId);
				return;
			}
			else
			{
				if (GetSessionId() != kInvalidSessionId)
				{
					const bool bound = m_accountId != 0 && m_userId != kInvalidRuntimeId;
					const eBootstrapKind bootstrapKind = bound ? ResolveServerBootstrapKind(m_userId) : eBootstrapKind::Pending;
					SendTcpBindResponse(*this, bound, m_accountId, bound ? m_userId : kInvalidRuntimeId, bootstrapKind);
					return;
				}
				if (!TryBeginServerBind())
					return;
				SetSessionState(eSessionState::Authenticating);

				const uint64 authAttempt = ++m_serverAuthAttempt;
				const EndpointHandle endpoint = GetEndpointHandle();
				const auto service = GetServiceRef();
				AuthenticateServerTcpBind(*req, [service, endpoint, authAttempt](uint64 accountId)
					{
						if (!service || !endpoint.IsValid())
							return;
						const auto shard = GLOBAL_EXEC.GetShard(endpoint.routeKey);
						if (!shard)
							return;
						shard->Submit(Job([service, endpoint, authAttempt, accountId]()
							{
								auto* self = static_cast<TcpSession*>(service->FindOwnedSession(kInvalidSessionId, endpoint));
								if (!self || self->m_serverAuthAttempt != authAttempt)
									return;
								if (accountId == 0)
								{
									self->SetSessionState(eSessionState::Binding);
									self->EndServerBind();
									SendTcpBindResponse(*self, false, 0, kInvalidRuntimeId);
									return;
								}

								const RouteKey boundRouteKey = MakeBoundSessionRouteKey(accountId);
								self->SetSessionState(eSessionState::Binding);
								self->Rehome(boundRouteKey, [self, accountId, boundRouteKey](bool rehomeOk)
									{
										if (!rehomeOk || self->GetRouteKey() != boundRouteKey || self->IsClosing())
										{
											self->EndServerBind();
											SendTcpBindResponse(*self, false, accountId, kInvalidRuntimeId);
											return;
										}
										const RuntimeId userId = self->ResolveServerTcpBindUserId(accountId);
										if (userId == kInvalidRuntimeId)
										{
											self->EndServerBind();
											SendTcpBindResponse(*self, false, accountId, kInvalidRuntimeId);
											return;
										}
										self->SetAccountId(accountId);
										self->SetUserId(userId);
										self->IssueSessionId();
										self->EndServerBind();
										SendTcpBindResponse(*self, self->GetSessionId() != kInvalidSessionId,
											accountId, self->m_userId, self->ResolveServerBootstrapKind(self->m_userId));
									});
							}));
					});
			}
			break;

		default: break;
		}
	}

	void TcpSession::TrySessionBinding()
	{
		if (!IsClientSide() || !IsConnected())
			return;
		if (m_clientBind.active || m_clientBind.bound)
			return;
		if (m_loginRequest.secretSize == 0)
			return;

		m_clientBind.active = true;
		m_clientBind.bound = false;
		m_clientBind.retryCount = 0;
		SetSessionState(eSessionState::Authenticating);
		auto pkt = PacketBuilder::CreateSystemPacket(eSystemPacketId::TCP_BIND_REQ, PacketFlags::NONE, eChannel::TCP_DEFAULT, &m_loginRequest, sizeof(m_loginRequest));
		Send(pkt);
		ScheduleSessionBindingRetry();
	}

	void TcpSession::ScheduleSessionBindingRetry()
	{
		const uint32 token = ++m_clientBind.timerToken;
		const EndpointHandle endpointHandle = GetEndpointHandle();
		const uint32 generation = GetServiceGeneration();
		auto service = GetServiceRef();
		SubmitAfter(Job([service, endpointHandle, generation, token]()
			{
				auto* self = service ? static_cast<TcpSession*>(service->FindOwnedSession(kInvalidSessionId, endpointHandle, generation)) : nullptr;
				if (!self)
					return;
				if (!self->m_clientBind.active || self->m_clientBind.bound || self->m_clientBind.timerToken != token)
					return;
				if (self->m_clientBind.retryCount >= kMaxBindingRetry)
				{
					self->m_clientBind.active = false;
					self->m_clientBind.bound = false;
					JAM_LOG_ERROR("[TcpSession] TCP bind failed or timed out. accountId={}", self->m_accountId);
					self->Disconnect();
					return;
				}

				self->m_clientBind.retryCount++;
				auto pkt = PacketBuilder::CreateSystemPacket(eSystemPacketId::TCP_BIND_REQ, PacketFlags::NONE, eChannel::TCP_DEFAULT, &self->m_loginRequest, sizeof(self->m_loginRequest));
				self->Send(pkt);
				self->ScheduleSessionBindingRetry();
			}, eJobPriority::Control), kClientBindRetryDelayNs);
	}



	void TcpSession::SendImmediatePacket(Packet packet)
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
