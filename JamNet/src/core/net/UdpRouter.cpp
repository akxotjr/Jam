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
		if (numOfBytes == 0 || !m_service) return;

		const RouteKey routeKey = Session::MakeUdpRouteKey(remoteAddr);
		const auto shard = GLOBAL_EXEC.GetShard(routeKey);
		if (!shard) return;

		shard->Submit(Job([this, remoteAddr, numOfBytes]() mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& table = GetUdpSessionTable(L);
				const SessionTableKey key{ m_socket, remoteAddr };

				if (auto it = table.find(key); it != table.end())
				{
					it->second->OnSend(numOfBytes);
				}
			}));
	}

	void UdpRouter::ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddr, Packet packet, uint64 ingressRecvTime_ns)
	{
		if (numOfBytes == 0 || !m_service) return;

		const RouteKey routeKey = Session::MakeUdpRouteKey(remoteAddr);
		const auto shard = GLOBAL_EXEC.GetShard(routeKey);
		if (!shard) return;

		shard->Submit(Job([this, remoteAddr, numOfBytes, pkt = std::move(packet), ingressRecvTime_ns]() mutable
			{
				auto& L     = CurrentShardLocalChecked();
				auto& table = GetUdpSessionTable(L);
				const SessionTableKey key{ m_socket, remoteAddr };

				UdpSession* session = nullptr;
				if (auto it = table.find(key); it != table.end())
				{
					session = it->second.get();
				}
				else
				{
					if (m_service->GetServiceType() != eServiceType::SERVER)
						return;

					auto owner = m_service->MakeUdpSession(remoteAddr);
					if (!owner) return;
					session = owner.get();

					if (table.emplace(key, std::move(owner)).second)
					{
						L.sessionState->logicalSessionIndex[session->GetSessionHandle()] = session;
						m_service->m_udpSessionCount.fetch_add(1, std::memory_order_relaxed);
						m_service->m_sessionCount.fetch_add(1, std::memory_order_relaxed);
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
}
