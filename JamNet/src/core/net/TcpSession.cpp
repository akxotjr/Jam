#include "pch.h"
#include "jamnet/core/net/TcpSession.h"

#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/memory/ObjectPool.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/net/Service.h"

namespace jam::net
{
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

	bool TcpSession::Connect()
	{
		if (!GetService() || !GetService()->RegisterIocpObject(this))
			return false;

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
			m_releaseQueued.store(true, std::memory_order_release);
			if (GetPendingDispatchCount() == 0)
				OnPendingDispatchDrained();
		}
	}

	void TcpSession::Send(Packet packet)
	{
		if (!packet.IsValid())
			return;

		const SessionHandle handle = GetSessionHandle();
		Post(Job([handle, packet]
			{
				auto& L = CurrentShardLocalChecked();
				auto* self = static_cast<TcpSession*>(FindSessionByHandle(L, handle));
				if (!self)
					return;

				const entt::entity e = self->EnsureSessionEntity();
				if (e == entt::null) return;

				SendPacketToSession(e, packet);
			}));
	}

	void TcpSession::OnLinkEstablished()
	{
		m_state.store(eSessionState::CONNECTED, std::memory_order_relaxed);
	}

	void TcpSession::OnLinkTerminated()
	{
		m_state.store(eSessionState::DISCONNECTED, std::memory_order_relaxed);
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
		if (SocketUtils::SetReuseAddress(m_socket, true) == false) return false;
		if (SocketUtils::BindAnyAddress(m_socket, 0) == false)	   return false;

		m_connectEvent.Init();
		if (!TryAddPendingDispatch())
			return false;

		DWORD		bytes	 = 0;
		SOCKADDR_IN sockAddr = GetService()->GetRemoteTcpNetAddress().GetSockAddr();

		if (SOCKET_ERROR == SocketUtils::ConnectEx(m_socket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr), nullptr, 0, &bytes, &m_connectEvent))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				ReleasePendingDispatch();
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
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
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
			const int32 ec = ::WSAGetLastError();
			if (ec != WSA_IO_PENDING)
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
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
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

		OnLinkEstablished();

		const SessionHandle handle = GetSessionHandle();
		Post(Job([handle]
			{
				auto& L = CurrentShardLocalChecked();
				auto* self = static_cast<TcpSession*>(FindSessionByHandle(L, handle));
				if (!self)
					return;

				if (self->EnsureSessionEntity() == entt::null)
					return;

				self->OnConnected();
			}, eJobPriority::Control));

		if (!IsClosing())
			RegisterRecv();
	}

	void TcpSession::ProcessOutboundConnect()
	{
		if (!SocketUtils::SetUpdateConnectSocket(m_socket))
		{
			JAMNET_LOG_ERROR("SO_UPDATE_CONNEXT_CONTEXT failed. ec= {}", ::WSAGetLastError());
			Disconnect();
			return;
		}

		OnLinkEstablished();

		const SessionHandle handle = GetSessionHandle();
		Post(Job([handle]()
			{
				auto& L = CurrentShardLocalChecked();
				auto* self = static_cast<TcpSession*>(FindSessionByHandle(L, handle));
				if (!self)
					return;

				if (self->EnsureSessionEntity() == entt::null)
					return;

				self->OnConnected();
			}, eJobPriority::Control));

		if (!IsClosing())
			RegisterRecv();
	}

	void TcpSession::ProcessDisconnect()
	{
		OnLinkTerminated();

		const SessionHandle handle = GetSessionHandle();
		Post(Job([handle]()
			{
				auto& L = CurrentShardLocalChecked();
				auto* self = static_cast<TcpSession*>(FindSessionByHandle(L, handle));
				if (!self)
					return;

				self->OnDisconnected();
			}, eJobPriority::Control));
		m_releaseQueued.store(true, std::memory_order_release);
	}

	void TcpSession::OnPendingDispatchDrained()
	{
		if (!m_releaseQueued.exchange(false, std::memory_order_acq_rel))
			return;

		auto* service = GetService();
		if (!service)
			return;

		service->ReleaseTcpSession(this);
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
			JAMNET_LOG_ERROR("TcpRecvAssembler::Append failed");
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
				JAMNET_LOG_ERROR("Tcp recv protocol error");
				ObjectPool<TcpRecvEvent>::Push(ev);
				Disconnect();
				return;
			}

			const SessionHandle handle = GetSessionHandle();
			Post(Job([handle, packet = std::move(pkt)]() mutable
				{
					auto& L = CurrentShardLocalChecked();
					auto* self = static_cast<TcpSession*>(FindSessionByHandle(L, handle));
					if (!self)
						return;

					const entt::entity e = self->EnsureSessionEntity();
					if (e != entt::null)
					{
						ProcessReceivedPacket(e, std::move(packet));
					}
				}, eJobPriority::Control));
		}

		ObjectPool<TcpRecvEvent>::Push(ev);
		if (!IsClosing())
			RegisterRecv();
	}


	//	부분전송 지원- 남은 구간을 재등록, 다 보냈으면 정리. 
	void TcpSession::ProcessSend(TcpSendEvent* ev,  int32 bytes)
	{
		if (!ev) return;

		if (bytes <= 0)
		{
			ev->chains.clear();
			ev->wsaBufs.clear();
			ObjectPool<TcpSendEvent>::Push(ev);
			Disconnect();
			JAMNET_LOG_WARN_LOC("[TcpSession] Send 0 byte");
			return;
		}

		// 상위 통지(이번 완료 바이트)
		OnSend(bytes);

		// 남은 바이트 계산
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

		// curIndex/curOffset 갱신
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

		// 다음 WSABUF 배열 구성(첫 요소 offset 적용)
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
			const int32 ec = ::WSAGetLastError();
			if (ec != WSA_IO_PENDING)
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
		JAMNET_LOG_WARN_LOC("[TcpSession] Handle error= {}", errorCode);
		switch (errorCode)
		{
		case WSAECONNRESET:
		case WSAECONNABORTED:
			Disconnect();
			break;
		default: break;
		}
	}

	entt::entity TcpSession::EnsureSessionEntity()
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
