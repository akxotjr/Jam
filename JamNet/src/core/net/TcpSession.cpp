#include "pch.h"
#include "jamnet/core/net/TcpSession.h"

#include "jambase/EnumUtils.h"
#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/memory/ObjectPool.h"
#include "jamnet/core/net/AdmissionContext.h"
#include "jamnet/core/net/RetryDelay.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/utils/Clock.h"
#include "jamnet/core/net/WinErrorHandling.h"

namespace jam::net
{

	namespace
	{
		inline constexpr uint8  kMaxBindingRetry		= 15;
		inline constexpr uint64 kClientBindRetryDelayNs = 2000_ms;
		inline constexpr uint64 kClientBindRetryJitterNs = 200_ms;

		bool IsTcpBindRequest(const PacketHeaderView& view)
		{
			if (!view.IsValid() || view.Type() != ePacketType::SYSTEM)
				return false;
			return ToEnum<eSystemPacketId>(view.Id()) == eSystemPacketId::TCP_BIND_REQ;
		}

	}


	TcpSession::TcpSession() 
		: m_recvAssembler(
			GetNetBufferPool(eNetBufferPoolKind::TcpIo),
			GetNetBufferPool(eNetBufferPoolKind::PacketSmall),
			GetNetBufferPool(eNetBufferPoolKind::PacketLarge))
	{
		m_protocol = eProtocolType::TCP;
		m_socket   = SocketUtils::CreateSocket(eProtocolType::TCP);
	}

	TcpSession::~TcpSession()
	{
		SocketUtils::Close(m_socket);
	}

	bool TcpSession::SetAuthCredential(uint32 scheme, std::span<const uint8> field0, std::span<const uint8> field1)
	{
		if (field0.size() > kMaxAuthFieldBytes || field1.size() > kMaxAuthFieldBytes)
			return false;

		m_authRequest = {};
		m_authRequest.scheme	 = scheme;
		m_authRequest.field0Size = static_cast<uint16>(field0.size());
		m_authRequest.field1Size = static_cast<uint16>(field1.size());
		if (!field0.empty())
			std::memcpy(m_authRequest.field0, field0.data(), field0.size());
		if (!field1.empty())
			std::memcpy(m_authRequest.field1, field1.data(), field1.size());
		m_hasAuthRequest = true;

		return true;
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
			&& (IsTcpBindRequest(view) || GetEntity() == entt::null || !CanCreateSessionEntity()))
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

		m_connected.store(true, std::memory_order_relaxed);

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

		m_connected.store(true, std::memory_order_relaxed);

		if (!IsClosing())
			RegisterRecv();

		const EndpointId endpointId = GetEndpointId();
		auto service = std::static_pointer_cast<ClientService>(GetServiceRef());
		Post(Job([service, endpointId]()
			{
				auto* self = service ? service->FindTcpSession() : nullptr;
				if (self && !self->MatchesEndpoint(endpointId)) self = nullptr;
				if (!self || self->IsClosing())
					return;

				self->TrySessionBinding();
			}, eJobPriority::Control));
	}

	void TcpSession::ProcessDisconnect()
	{
		m_connected.store(false, std::memory_order_relaxed);
		m_clientBind.active = false;
		++m_clientBind.timerToken;
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
		if (IsServerSide() && sessionId == kInvalidSessionId)
		{
			if (auto admission = m_admissionContext.lock(); admission && m_admissionId != 0)
			{
				admission->RequestTcpRelease(AdmissionKey{
					.admissionId	= m_admissionId,
					.protocol		= eProtocolType::TCP,
					.endpointId		= GetEndpointId(),
				}, this);
				return;
			}
			return;
		}

		if (service->GetServiceType() == eServiceType::CLIENT)
		{
			auto clientService = std::static_pointer_cast<ClientService>(service);
			clientService->GetPrincipalShard()->Submit(Job([clientService, expected = this]
				{
					auto* self = clientService->FindTcpSession();
					if (self != expected) return;
					clientService->ReleaseTcpSession(self);
				}, eJobPriority::Control));
		}
		else
		{
			service->ReleaseTcpSession(this);
		}
	}


	void TcpSession::SetAdmissionContext(std::shared_ptr<AdmissionContext> context, uint64 admissionId)
	{
		m_admissionContext = context;
		m_admissionId = admissionId;
	}

	void TcpSession::ClearAdmissionContext()
	{
		m_admissionContext.reset();
		m_admissionId = 0;
	}


	void TcpSession::ProcessRecv(TcpRecvEvent* ev, int32 bytes)
	{
		if (!ev) return;

		const ULONG_PTR nativeStatus = win_error::GetOverlappedNativeStatus(*ev);
		if (!win_error::IsNativeStatusSuccess(nativeStatus))
		{
			const int32 errorCode = static_cast<int32>(win_error::NativeStatusToWinError(nativeStatus));
			ObjectPool<TcpRecvEvent>::Push(ev);
			HandleError(errorCode);
			return;
		}

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

		std::vector<Packet> packets;
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

			const EndpointId endpointId = GetEndpointId();
			const SessionId sessionId = GetSessionId();
			if (IsServerSide() && sessionId == kInvalidSessionId)
			{
				if (auto admission = m_admissionContext.lock(); admission && m_admissionId != 0)
				{
					admission->OnTcpPacket(AdmissionKey{
						.admissionId	= m_admissionId,
						.protocol		= eProtocolType::TCP,
						.endpointId		= endpointId,
					}, std::move(pkt));
					continue;
				}

				ObjectPool<TcpRecvEvent>::Push(ev);
				Disconnect();
				return;
			}

			packets.push_back(std::move(pkt));
		}

		if (!packets.empty())
		{
			Post(Job([this, packets = std::move(packets)]() mutable
				{
					for (auto& packet : packets)
					{
						PacketHeaderView directView = PacketHeaderView::Parse(packet->Head(), packet->Size());
						if (directView.IsValid() && directView.TotalSize() == packet->Size() && directView.Type() == ePacketType::SYSTEM)
						{
							ProcessSystemPacket(std::move(packet), directView, 0_ns);
							continue;
						}

						const entt::entity e = GetEntity();
						if (e != entt::null)
							ProcessReceivedPacket(e, std::move(packet));
					}
				}, eJobPriority::Control));
		}

		ObjectPool<TcpRecvEvent>::Push(ev);
		if (!IsClosing())
			RegisterRecv();
	}


	void TcpSession::ProcessSend(TcpSendEvent* ev,  int32 bytes)
	{
		if (!ev) return;

		const ULONG_PTR nativeStatus = win_error::GetOverlappedNativeStatus(*ev);
		if (!win_error::IsNativeStatusSuccess(nativeStatus))
		{
			const int32 errorCode = static_cast<int32>(win_error::NativeStatusToWinError(nativeStatus));
			ev->chains.clear();
			ev->wsaBufs.clear();
			ObjectPool<TcpSendEvent>::Push(ev);
			HandleError(errorCode);
			return;
		}

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
		case WSAENOTCONN:
		case WSAESHUTDOWN:
		case WSAECONNRESET:
		case WSAECONNABORTED:
		case ERROR_NETNAME_DELETED:
		case ERROR_CONNECTION_ABORTED:
			Disconnect();
			break;
		case ERROR_OPERATION_ABORTED:
			if (!IsClosing())
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
			if (IsClientSide())
				HandleClientBindResponse(view);
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

	void TcpSession::HandleClientBindResponse(const PacketHeaderView& view)
	{
		if (!IsClientSide() || IsClosing() || !m_clientBind.active || m_clientBind.bound)
			return;

		if (ToEnum<eSystemPacketId>(view.Id()) != eSystemPacketId::TCP_BIND_RES || view.PayloadSize() < sizeof(TCP_BIND_RES_DATA))
			return;

		const auto* res = reinterpret_cast<const TCP_BIND_RES_DATA*>(view.Payload());
		m_clientBind.active = false;
		++m_clientBind.timerToken;
		if (!res->success || res->userId == 0 || res->sessionId == kInvalidSessionId)
		{
			m_clientBind.bound = false;
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
			m_clientBind.bound = false;
			Disconnect();
			return;
		}
		CompleteSessionEstablishment(false);
		OnTcpBindBootstrap(res->bootstrapKind);
	}

	void TcpSession::TrySessionBinding()
	{
		if (!IsClientSide() || !IsConnected())
			return;
		if (m_clientBind.active || m_clientBind.bound)
			return;
		if (!m_hasAuthRequest)
			return;

		m_clientBind.active = true;
		m_clientBind.bound = false;
		m_clientBind.retryCount = 0;
		auto pkt = PacketBuilder::CreateTcpBindReqPacket(m_authRequest);
		Send(pkt);
		ScheduleSessionBindingRetry();
	}

	void TcpSession::ScheduleSessionBindingRetry()
	{
		const uint32 token = ++m_clientBind.timerToken;
		const EndpointId endpointId = GetEndpointId();
		auto service = std::static_pointer_cast<ClientService>(GetServiceRef());
		SubmitAfter(Job([service, endpointId, token]()
			{
				auto* self = service ? service->FindTcpSession() : nullptr;
				if (self && !self->MatchesEndpoint(endpointId)) self = nullptr;
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
				auto pkt = PacketBuilder::CreateTcpBindReqPacket(self->m_authRequest);
				self->Send(pkt);
				self->ScheduleSessionBindingRetry();
			}, eJobPriority::Control), MakeRetryDelayNs(kClientBindRetryDelayNs, kClientBindRetryJitterNs, m_endpointId, token));
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
