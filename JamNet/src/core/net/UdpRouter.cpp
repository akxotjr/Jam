#include "pch.h"
#include "jamnet/core/net/UdpRouter.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/memory/ObjectPool.h"
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/PacketWireTime.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/UdpSession.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/net/WinErrorHandling.h"

#pragma warning(disable : 4996)

namespace jam::net
{
	size_t UdpRouter::StartIngressIndex(uint64 endpointId)
	{
		return static_cast<size_t>((endpointId * 11400714819323198485ull) & (INGRESS_TABLE_CAPACITY - 1));
	}

	void UdpRouter::UpsertIngressRoute(uint64 endpointId, UdpIngressRouteKind kind, uint64 value, uint32 generation)
	{
		if (endpointId == kEmptyIngressKey || endpointId == kTombstoneIngressKey)
			return;

		size_t idx = StartIngressIndex(endpointId);
		for (size_t probe = 0; probe < INGRESS_TABLE_CAPACITY; ++probe, idx = (idx + 1) & (INGRESS_TABLE_CAPACITY - 1))
		{
			auto& slot = m_ingressRoutes[idx];
			uint64 key = slot.key.load(std::memory_order_acquire);
			if (key == endpointId || key == kEmptyIngressKey || key == kTombstoneIngressKey)
			{
				uint32 sequence = slot.sequence.load(std::memory_order_acquire);
				if ((sequence & 1u) != 0 || !slot.sequence.compare_exchange_strong(sequence, sequence + 1, std::memory_order_acq_rel))
				{
					--probe;
					continue;
				}

				key = slot.key.load(std::memory_order_acquire);
				if (key != endpointId && key != kEmptyIngressKey && key != kTombstoneIngressKey)
				{
					slot.sequence.store(sequence + 2, std::memory_order_release);
					--probe;
					continue;
				}

				slot.key.store(endpointId, std::memory_order_relaxed);
				slot.value.store(value, std::memory_order_relaxed);
				slot.generation.store(generation, std::memory_order_relaxed);
				slot.kind.store(static_cast<uint8>(kind), std::memory_order_relaxed);
				slot.sequence.store(sequence + 2, std::memory_order_release);
				return;
			}
		}
	}

	bool UdpRouter::Start(Service* service)
	{
		m_service = service;
		if (!m_service) return false;

		m_socket = SocketUtils::CreateSocket(eProtocolType::UDP);
		if (m_socket == INVALID_SOCKET)
			return false;

		if (m_service->RegisterIocpObject(this) == false)
			return false;

		if (SocketUtils::SetReuseAddress(m_socket, true) == false)
			return false;

		if (m_service->GetServiceType() == eServiceType::CLIENT)
		{
			if (SocketUtils::BindAnyAddress(m_socket, 0) == false)
				return false;
		}
		else if (m_service->GetServiceType() == eServiceType::SERVER)
		{
			if (SocketUtils::Bind(m_socket, m_service->GetLocalUdpNetAddress()) == false)
				return false;
		}

		for (int8 i = 0; i < OUTSTANDING_RECVS; ++i)
		{
			RegisterRecv();
		}

		return true;
	}

	void UdpRouter::CloseSocket()
	{
		MarkClosing();
		SocketUtils::Close(m_socket);
	}


	HANDLE UdpRouter::GetHandle()
	{
		return reinterpret_cast<HANDLE>(m_socket);
	}

	void UdpRouter::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
	{
		switch (iocpEvent->m_eventType)
		{
		case eEventType::UdpRecv:
		{
			auto* ev = static_cast<UdpRecvEvent*>(iocpEvent);
			if (ev->packet.IsValid() && numOfBytes > 0)
			{
				auto& s = ev->packet.Get();
				if (s.TryAppendPayload(static_cast<uint32>(numOfBytes)))
				{
					s.CloseWithCommit(static_cast<uint32>(numOfBytes));
					const uint64 ingress = CaptureWireTimestampNow();
					ProcessRecv(numOfBytes, ev->remoteAddr, ev->packet, ingress);
				}
			}

			ev->packet.Reset();
			ObjectPool<UdpRecvEvent>::Push(ev);
			RegisterRecv();
			break;
		}
		case eEventType::UdpSend:
		{
			auto* ev = static_cast<UdpSendEvent*>(iocpEvent);

			if (numOfBytes > 0)
				ProcessSend(numOfBytes, ev->remoteAddr);
			ev->chain.Clear();
			ev->wsaBufs.clear();
			ObjectPool<UdpSendEvent>::Push(ev);
			break;
		}
		default:
			break;
		}
	}


	void UdpRouter::RegisterSend(std::vector<PacketChain>&& chains, const NetAddress& to)
	{
		if (IsClosing())
			return;

		for (auto& chain : chains)
		{
			if (chain.Empty()) continue;

			auto* ev = ObjectPool<UdpSendEvent>::Pop();
			ev->Init();
			ev->remoteAddr = to;
			ev->chain      = std::move(chain);

			const auto& parts = ev->chain.Parts();
			ev->wsaBufs.clear();
			ev->wsaBufs.reserve(parts.size());

			for (const BufferSlice& part : parts)
			{
				if (!part.IsValid() || part.Size() == 0)
					continue;

				WSABUF w{};
				w.buf = reinterpret_cast<char*>(part.Head());
				w.len = static_cast<ULONG>(part.Size());
				ev->wsaBufs.push_back(w);
			}

			if (ev->wsaBufs.empty())
			{
				ev->chain.Clear();
				ev->wsaBufs.clear();
				ObjectPool<UdpSendEvent>::Push(ev);
				continue;
			}

			PatchOutgoingSystemWireTime(ev->chain, CaptureWireTimestampNow());

			if (!TryAddPendingDispatch())
			{
				ev->chain.Clear();
				ev->wsaBufs.clear();
				ObjectPool<UdpSendEvent>::Push(ev);
				continue;
			}

			if (SOCKET_ERROR == ::WSASendTo(m_socket, ev->wsaBufs.data(), static_cast<DWORD>(ev->wsaBufs.size()), nullptr, 0, ev->remoteAddr.GetSockAddrPtr(), sizeof(SOCKADDR_IN), ev, nullptr))
			{
				const int32 ec = win_error::GetLastWsaError();
				if (!win_error::IsIoPending(ec))
				{
					ReleasePendingDispatch();
					HandleError(ec);
					ev->chain.Clear();
					ev->wsaBufs.clear();
					ObjectPool<UdpSendEvent>::Push(ev);
				}
			}
		}
	}

	void UdpRouter::RegisterRecv()
	{
		if (IsClosing())
			return;

		eNetBufferPoolKind kind = m_service->GetServiceType() == eServiceType::CLIENT ?
			eNetBufferPoolKind::UdpClientIo : eNetBufferPoolKind::UdpServerIo;

		BufWriter writer(GetNetBufferPool(kind));
		BufferSlice slice = writer.OpenForPayload(JAMNET_MTU, alignof(PacketHeader));

		auto* ev  = ObjectPool<UdpRecvEvent>::Pop();
		ev->Init();
		ev->packet          = MakeOwned(slice);
		ev->wsaBuf.buf      = reinterpret_cast<char*>(ev->packet->Head());
		ev->wsaBuf.len      = ev->packet->Capacity();
		ev->remoteAddrLen   = sizeof(SOCKADDR_IN);

		if (!TryAddPendingDispatch())
		{
			ev->packet.Reset();
			ObjectPool<UdpRecvEvent>::Push(ev);
			return;
		}


		if (SOCKET_ERROR == ::WSARecvFrom(m_socket, &ev->wsaBuf, 1, nullptr, &ev->flags, ev->remoteAddr.GetSockAddrPtr(), OUT &ev->remoteAddrLen, ev, nullptr))
		{
			const int32 error = win_error::GetLastWsaError();
			if (!win_error::IsIoPending(error))
			{
				ReleasePendingDispatch();
				HandleError(error);
				ev->packet.Reset();
				ObjectPool<UdpRecvEvent>::Push(ev);
			}
		}
	}

	void UdpRouter::ProcessSend(int32 numOfBytes, const NetAddress& remoteAddr)
	{
		if (numOfBytes == 0)
		{
			JAMNET_LOG_WARN_LOC("[ip= {}, port= {}] num of bytes is 0", remoteAddr.GetIpAddress(), remoteAddr.GetPort());
		}
	}

	void UdpRouter::ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddr, Packet packet, uint64 ingressRecvTime_ns)
	{
		if (numOfBytes == 0 || !m_service) return;

		const uint64 endpointId = Session::MakeEndpointId(remoteAddr);
		UdpIngressRoute ingressRoute = {};
		RouteKey routeKey = Session::MakeUdpRouteKey(remoteAddr);
		if (TryGetIngressRoute(endpointId, ingressRoute))
		{
			if (ingressRoute.kind == UdpIngressRouteKind::PrebindRoute && IsValidRouteKey(ingressRoute.routeKey))
				routeKey = ingressRoute.routeKey;
			else if (ingressRoute.kind == UdpIngressRouteKind::BoundSession && ingressRoute.sessionId != kInvalidSessionId)
			{
				//if (ingressRoute.sessionId == 281479271677953)
				//	JAMNET_LOG_DEBUG("[UdpRouter::ProcessRecv] sessionId= {} receive packet", 281479271677953);

				const uint16 targetShardIndex = GetRuntimeShardIndex(ingressRoute.sessionId);
				if (const auto boundShard = GLOBAL_EXEC.GetShardFromIndex(targetShardIndex))
				{
					boundShard->Submit(Job([numOfBytes, ingressRecvTime_ns, endpointId, targetShardIndex, sessionId = ingressRoute.sessionId, pkt = std::move(packet)]() mutable
						{
							auto& state = GetOrCreateSessionShardState(CurrentShardLocalChecked());
							if (auto* forwarded = static_cast<UdpSession*>(state.FindSession(sessionId)))
							{
								//if (sessionId == 281479271677953)
								//	JAMNET_LOG_DEBUG("[UdpRouter::ProcessRecv] sessionId= {} forward packet", 281479271677953);
								forwarded->ProcessRecv(numOfBytes, std::move(pkt), ingressRecvTime_ns);
							}
							else
							{
								JAMNET_LOG_WARN(
									"[UdpRouter] Bound UDP session lookup failed. endpointId={} sessionId={} targetShard={} localShard={}",
									endpointId,
									sessionId,
									targetShardIndex,
									CurrentShardLocalChecked().shardIndex);
							}
						}, eJobPriority::Control));
				}
				else
				{
					JAMNET_LOG_WARN(
						"[UdpRouter] Bound UDP shard resolve failed. endpointId={} sessionId={} targetShard={}",
						endpointId,
						ingressRoute.sessionId,
						targetShardIndex);
				}
				return;
			}
		}

		auto service = m_service->shared_from_this();
		const auto shard = service->GetServiceType() == eServiceType::CLIENT ? std::static_pointer_cast<ClientService>(service)->GetPrincipalShard() : GLOBAL_EXEC.GetShard(routeKey);
		if (!shard) return;

		const uint32 routeGeneration = ingressRoute.generation;
		shard->Submit(Job([service = std::move(service), routeKey, remoteAddr, endpointId, routeGeneration, numOfBytes, pkt = std::move(packet), ingressRecvTime_ns]() mutable
			{
				const EndpointHandle handle{ routeKey, endpointId };

				UdpSession* session = static_cast<UdpSession*>(service->FindOwnedSession(kInvalidSessionId, handle, routeGeneration));

				if (!session)
				{
					if (!service->IsRunning())
						return;
					if (service->GetServiceType() != eServiceType::SERVER)
						return;
					auto& state = GetOrCreateSessionShardState(CurrentShardLocalChecked());

					auto owner = service->MakeUdpSession(remoteAddr);
					if (!owner) return;
					session = owner.get();

					if (state.AttachPreboundSession(std::move(owner)))
					{
						service->m_udpSessionCount.fetch_add(1, std::memory_order_relaxed);
						service->m_sessionCount.fetch_add(1, std::memory_order_relaxed);
					}
					else
					{
						session = nullptr;
					}
				}

				if (session)
					session->ProcessRecv(numOfBytes, std::move(pkt), ingressRecvTime_ns);

			}, eJobPriority::Critical));
	}

	void UdpRouter::HandleError(int32 errorCode)
	{
		switch (errorCode)
		{
		case WSAECONNRESET:
		case WSAECONNABORTED:
			break;
		default:
			win_error::LogWsaError("[UdpRouter] socket operation", errorCode);
			break; 
		}
	}

	void UdpRouter::RegisterIngressPrebindRoute(uint64 endpointId, RouteKey ownerRouteKey, uint32 generation)
	{
		if (!IsValidRouteKey(ownerRouteKey))
		{
			ClearIngressRoute(endpointId);
			return;
		}

		UpsertIngressRoute(endpointId, UdpIngressRouteKind::PrebindRoute, ownerRouteKey.value(), generation);
	}

	void UdpRouter::PromoteIngressToBound(uint64 endpointId, SessionId sessionId)
	{
		if (sessionId == kInvalidSessionId)
		{
			ClearIngressRoute(endpointId);
			return;
		}

		UpsertIngressRoute(endpointId, UdpIngressRouteKind::BoundSession, static_cast<uint64>(sessionId));
	}

	void UdpRouter::ClearIngressRoute(uint64 endpointId)
	{
		if (endpointId == kEmptyIngressKey || endpointId == kTombstoneIngressKey)
			return;

		size_t idx = StartIngressIndex(endpointId);
		for (size_t probe = 0; probe < INGRESS_TABLE_CAPACITY; ++probe, idx = (idx + 1) & (INGRESS_TABLE_CAPACITY - 1))
		{
			auto& slot = m_ingressRoutes[idx];
			const uint64 key = slot.key.load(std::memory_order_acquire);
			if (key == kEmptyIngressKey)
				return;
			if (key != endpointId)
				continue;

			uint32 sequence = slot.sequence.load(std::memory_order_acquire);
			if ((sequence & 1u) != 0 ||
				!slot.sequence.compare_exchange_strong(sequence, sequence + 1, std::memory_order_acq_rel))
			{
				--probe;
				continue;
			}

			if (slot.key.load(std::memory_order_acquire) != endpointId)
			{
				slot.sequence.store(sequence + 2, std::memory_order_release);
				--probe;
				continue;
			}

			slot.kind.store(static_cast<uint8>(UdpIngressRouteKind::None), std::memory_order_relaxed);
			slot.value.store(0, std::memory_order_relaxed);
			slot.generation.store(0, std::memory_order_relaxed);
			slot.key.store(kTombstoneIngressKey, std::memory_order_relaxed);
			slot.sequence.store(sequence + 2, std::memory_order_release);
			return;
		}
	}

	bool UdpRouter::TryGetIngressRoute(uint64 endpointId, UdpIngressRoute& out) const
	{
		if (endpointId == kEmptyIngressKey || endpointId == kTombstoneIngressKey)
			return false;

		size_t idx = StartIngressIndex(endpointId);
		for (size_t probe = 0; probe < INGRESS_TABLE_CAPACITY; ++probe, idx = (idx + 1) & (INGRESS_TABLE_CAPACITY - 1))
		{
			const auto& slot = m_ingressRoutes[idx];
			const uint64 key = slot.key.load(std::memory_order_acquire);
			if (key == kEmptyIngressKey)
				return false;
			if (key != endpointId)
				continue;

			const uint32 sequence = slot.sequence.load(std::memory_order_acquire);
			if ((sequence & 1u) != 0)
			{
				--probe;
				continue;
			}

			const auto kind = static_cast<UdpIngressRouteKind>(slot.kind.load(std::memory_order_relaxed));
			if (kind == UdpIngressRouteKind::None)
				return false;

			const uint64 value = slot.value.load(std::memory_order_relaxed);
			const uint32 generation = slot.generation.load(std::memory_order_relaxed);
			if (slot.sequence.load(std::memory_order_acquire) != sequence ||
				slot.key.load(std::memory_order_acquire) != endpointId)
			{
				--probe;
				continue;
			}
			out.kind      = kind;
			out.routeKey  = (kind == UdpIngressRouteKind::PrebindRoute) ? RouteKey(value) : RouteKey{};
			out.sessionId = (kind == UdpIngressRouteKind::BoundSession) ? static_cast<SessionId>(value) : kInvalidSessionId;
			out.generation = generation;
			return true;
		}

		return false;
	}
}
