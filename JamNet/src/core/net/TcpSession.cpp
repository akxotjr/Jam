#include "pch.h"
#include "jamnet/core/net/TcpSession.h"

#include "jamnet/core/executor/Job.h"
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
		return RegisterConnect();
	}

	void TcpSession::Disconnect()
	{
		RegisterDisconnect();
	}

	void TcpSession::Send(Packet packet)
	{
		if (!packet.IsValid())
			return;

		auto self = static_pointer_cast<TcpSession>(shared_from_this());
		Post(Job([self, packet]
			{
				const entt::entity e = self->GetEntity();
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
			ProcessConnect();
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
		m_connectEvent.m_owner = shared_from_this();

		DWORD		bytes	 = 0;
		SOCKADDR_IN sockAddr = GetService()->GetRemoteTcpNetAddress().GetSockAddr();
		if (SOCKET_ERROR == SocketUtils::ConnectEx(m_socket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr), nullptr, 0, &bytes, &m_connectEvent))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				m_connectEvent.m_owner = nullptr;
				return false;
			}
		}
		return true;
	}

	bool TcpSession::RegisterDisconnect()
	{
		m_disconnectEvent.Init();
		m_disconnectEvent.m_owner = shared_from_this();

		if (false == SocketUtils::DisconnectEx(m_socket, &m_disconnectEvent, TF_REUSE_SOCKET, 0))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				m_disconnectEvent.m_owner = nullptr;
				return false;
			}
		}
		return true;
	}

	void TcpSession::RegisterSend(std::vector<PacketChain>&& chains)
	{
		auto* ev = ObjectPool<TcpSendEvent>::Pop();
		ev->Init();
		ev->m_owner    = shared_from_this();
		ev->chains     = std::move(chains);
		ev->curIndex   = 0;
		ev->curOffset  = 0;
		ev->totalBytes = 0;
		ev->wsaBufs.clear();
		size_t partCount = 0;
		for (const auto& chain : ev->chains)
			partCount += chain.Count();
		ev->wsaBufs.reserve(partCount);

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

		DWORD sent = 0;
		if (SOCKET_ERROR == ::WSASend(m_socket, ev->wsaBufs.data(), static_cast<DWORD>(ev->wsaBufs.size()), OUT & sent, 0, ev, nullptr))
		{
			const int32 ec = ::WSAGetLastError();
			if (ec != WSA_IO_PENDING)
			{
				HandleError(ec);
				ev->chains.clear();
				ev->wsaBufs.clear();
				ObjectPool<TcpSendEvent>::Push(ev);
			}
		}
	}


	void TcpSession::RegisterRecv()
	{
		auto* ev = ObjectPool<TcpRecvEvent>::Pop();
		ev->Init();
		ev->m_owner    = shared_from_this();
		ev->wsaBuf.buf = reinterpret_cast<char*>(ev->buffer.data());
		ev->wsaBuf.len = static_cast<ULONG>(ev->buffer.size());

		DWORD bytes = 0;
		DWORD flags = 0;
		if (SOCKET_ERROR == ::WSARecv(m_socket, &ev->wsaBuf, 1, OUT &bytes, OUT &flags, ev, nullptr))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				HandleError(errorCode);
				ObjectPool<TcpRecvEvent>::Push(ev);
			}
		}
	}


	void TcpSession::ProcessConnect()
	{
		m_connectEvent.m_owner = nullptr;

		GetService()->RegisterTcpSession(static_pointer_cast<TcpSession>(shared_from_this()));

		auto self = static_pointer_cast<TcpSession>(shared_from_this());

		self->OnLinkEstablished();
		self->Post(Job(self, &TcpSession::OnConnected, eJobPriority::Control));

		RegisterRecv();
	}

	void TcpSession::ProcessDisconnect()
	{
		m_disconnectEvent.m_owner = nullptr;

		auto self = static_pointer_cast<TcpSession>(shared_from_this());
		self->OnLinkTerminated();
		self->Post(Job(self, &TcpSession::OnDisconnected, eJobPriority::Control));

		GetService()->ReleaseTcpSession(static_pointer_cast<TcpSession>(shared_from_this()));
	}

	void TcpSession::ProcessRecv(const TcpRecvEvent* ev, int32 bytes)
	{
		if (!ev) return;

		if (bytes == 0)
		{
			Disconnect();
			return;
		}

		if (!m_recvAssembler.Append(ev->buffer.data(), bytes))
		{
			JAMNET_LOG_ERROR("TcpRecvAssembler::Append failed");
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
				Disconnect();
				return;
			}

			auto self = std::static_pointer_cast<TcpSession>(shared_from_this());
			self->Post(Job([self, packet = std::move(pkt)]() mutable
				{
					const entt::entity e = self->GetEntity();
					if (e != entt::null)
					{
						ProcessReceivedPacket(e, std::move(packet));
					}
				}, eJobPriority::Control));
		}

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

		DWORD sent = 0;
		if (SOCKET_ERROR == ::WSASend(m_socket, bufs.data(), static_cast<DWORD>(bufs.size()), OUT & sent, 0, ev, nullptr))
		{
			const int32 ec = ::WSAGetLastError();
			if (ec != WSA_IO_PENDING)
			{
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
}
