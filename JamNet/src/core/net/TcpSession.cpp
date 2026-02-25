#include "pch.h"
#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/net/SessionSystems.h"

namespace jam::net
{
	/*-----------------
		TcpSession
	------------------*/

	TcpSession::TcpSession() 
	{
		m_protocol = eProtocolType::TCP;
		m_socket = SocketUtils::CreateSocket(eProtocolType::TCP);
		m_streamBuffer.Init(BUFFER_SIZE, 1);
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

	void TcpSession::Send(const std::shared_ptr<SendBuffer>& buf)
	{
		if (!buf || !buf->Buffer())
			return;

		auto self = static_pointer_cast<TcpSession>(shared_from_this());
		Post(Job([self, buf]
			{
				const entt::entity e = self->GetEntity();
				if (e == entt::null) return;

				SendPacketToSession(e, buf);
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

	void TcpSession::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
	{
		switch (iocpEvent->m_eventType)
		{
		case eEventType::CONNECT:
			ProcessConnect();
			break;

		case eEventType::DISCONNECT:
			ProcessDisconnect();
			break;

		case eEventType::RECV:
			ProcessRecv(static_cast<RecvEvent*>(iocpEvent), numOfBytes);
			break;

		case eEventType::SEND:
			ProcessSend(static_cast<SendEvent*>(iocpEvent), numOfBytes);
			break;

		default:break;
		}
	}

	bool TcpSession::RegisterConnect()
	{
		if (SocketUtils::SetReuseAddress(m_socket, true) == false) return false;
		if (SocketUtils::BindAnyAddress(m_socket, 0) == false) return false;

		m_connectEvent.Init();
		m_connectEvent.m_owner = shared_from_this();

		DWORD numOfBytes = 0;
		SOCKADDR_IN sockAddr = GetService()->GetRemoteTcpNetAddress().GetSockAddr();
		if (SOCKET_ERROR == SocketUtils::ConnectEx(m_socket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr), nullptr, 0, &numOfBytes, &m_connectEvent))
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


	void TcpSession::RegisterRecv()
	{
		auto* ev = ObjectPool<RecvEvent>::Pop();
		ev->Init();
		ev->m_owner = shared_from_this();
		ev->fromLen = 0; // TCP는 발신자 주소 없음

		auto* buf = ObjectPool<RecvBuffer>::Pop();
		buf->Init(BUFFER_SIZE, 1);
		ev->recvBuffer = buf;

		WSABUF wsaBuf;
		wsaBuf.buf = reinterpret_cast<char*>(buf->WritePos());
		wsaBuf.len = buf->FreeSize();

		DWORD numOfBytes = 0;
		DWORD flags = 0;
		if (SOCKET_ERROR == ::WSARecv(m_socket, &wsaBuf, 1, OUT &numOfBytes, OUT &flags, ev, nullptr))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				HandleError(errorCode);
				ObjectPool<RecvBuffer>::Push(buf);
				ObjectPool<RecvEvent>::Push(ev);
			}
		}
	}


	void TcpSession::RegisterSend(const std::vector<std::shared_ptr<SendBuffer>>& bufs)
	{
		auto* ev = ObjectPool<SendEvent>::Pop();
		ev->Init();
		ev->m_owner		= shared_from_this();
		ev->use_gather	= true;
		ev->sendBuffers = bufs;       // 생존 보장
		ev->gather.clear();
		ev->gather.reserve(bufs.size());
		ev->curIndex	= 0;
		ev->curOffset	= 0;
		ev->totalBytes	= 0;

		for (auto& sb : bufs)
		{
			WSABUF w;
			w.buf = reinterpret_cast<char*>(sb->Buffer());
			w.len = static_cast<ULONG>(sb->WriteSize());
			ev->gather.push_back(w);
			ev->totalBytes += w.len;
		}

		DWORD sent = 0;
		if (SOCKET_ERROR == ::WSASend(m_socket, ev->gather.data(), static_cast<DWORD>(ev->gather.size()), OUT &sent, 0, ev, nullptr))
		{
			const int32 ec = ::WSAGetLastError();
			if (ec != WSA_IO_PENDING)
			{
				HandleError(ec);
				ev->sendBuffers.clear();
				ev->gather.clear();
				ObjectPool<SendEvent>::Push(ev);
			}
		}
	}


	void TcpSession::ProcessConnect()
	{
		m_connectEvent.m_owner = nullptr;

		GetService()->RegisterTcpSession(static_pointer_cast<TcpSession>(shared_from_this()));

		auto self = static_pointer_cast<TcpSession>(shared_from_this());
		self->Post(Job(self, &TcpSession::OnConnected, eJobPriority::CTRL));

		RegisterRecv();
	}

	void TcpSession::ProcessDisconnect()
	{
		m_disconnectEvent.m_owner = nullptr;

		auto self = static_pointer_cast<TcpSession>(shared_from_this());
		self->Post(Job(self, &TcpSession::OnDisconnected, eJobPriority::CTRL));

		GetService()->ReleaseTcpSession(static_pointer_cast<TcpSession>(shared_from_this()));
	}

	void TcpSession::ProcessRecv(RecvEvent* ev, int32 numOfBytes)
	{
		if (!ev) return;

		RecvBuffer* ioBuf = ev->recvBuffer;

		if (!ioBuf)
		{
			ObjectPool<RecvEvent>::Push(ev);
			return;
		}

		if (numOfBytes <= 0)
		{
			ObjectPool<RecvBuffer>::Push(ioBuf);
			ev->recvBuffer = nullptr;
			ObjectPool<RecvEvent>::Push(ev);
			Disconnect();

			JAMNET_LOG_WARN_LOC("[TcpSession] Receive 0 byte");
			return;
		}

		if (!ioBuf->OnWrite(numOfBytes))
		{
			ObjectPool<RecvBuffer>::Push(ioBuf);
			ev->recvBuffer = nullptr;
			ObjectPool<RecvEvent>::Push(ev);
			Disconnect();

			JAMNET_LOG_WARN_LOC("[TcpSession] OnWrite Overflow");
			return;
		}

		const uint32 sz = static_cast<uint32>(ioBuf->DataSize());
		std::shared_ptr<RecvBuffer> snap = RecvBuffer::FromSpan(ioBuf->ReadPos(), sz);

		auto self = static_pointer_cast<TcpSession>(shared_from_this());
		self->Post(Job([self, snap]
			{
				self->ProcessRecvOnShard(snap);
			},
			eJobPriority::CTRL
		));


		ObjectPool<RecvBuffer>::Push(ioBuf);
		ev->recvBuffer = nullptr;
		ObjectPool<RecvEvent>::Push(ev);

		RegisterRecv();
	}

	void TcpSession::ProcessRecvOnShard(const std::shared_ptr<RecvBuffer>& snap)
	{
		if (!snap) return;
		const int32 size = snap->DataSize();
		if (size <= 0) return;

		// 누적
		if (m_streamBuffer.FreeSize() < size)
		{
			Disconnect();
			JAMNET_LOG_WARN_LOC("[TcpSession] OnWrite Overflow");
			return;
		}
		::memcpy(m_streamBuffer.WritePos(), snap->ReadPos(), size);
		if (!m_streamBuffer.OnWrite(size))
		{
			Disconnect();
			JAMNET_LOG_WARN_LOC("[TcpSession] OnWrite Overflow");
			return;
		}

		// 프레이밍: [TcpPacketHeader][payload] 반복
		int32 processed = 0;
		while (true)
		{
			const int32 available = m_streamBuffer.DataSize() - processed;
			if (available < static_cast<int32>(PacketHeader::BASE_SIZE))
				break;

			BYTE* base = m_streamBuffer.ReadPos() + processed;

			// PacketView로 파싱 시도
			PacketView view = PacketView::Parse(base, static_cast<uint32>(available));

			// 헤더가 불완전하면 대기
			if (!view.IsValid())
			{
				// 최소 헤더조차 없으면 대기
				if (available < static_cast<int32>(PacketHeader::BASE_SIZE))
					break;

				// 헤더는 있지만 검증 실패 → 연결 종료
				Disconnect();
				JAMNET_LOG_WARN_LOC("[TcpSession] Invalid Packet Header");
				return;
			}

			const uint32 headerSize = view.HeaderSize();
			const uint32 totalSize = view.TotalSize();

			if (totalSize < headerSize)
			{
				Disconnect();
				JAMNET_LOG_WARN_LOC("[TcpSession] Invalid Packet size");
				return;
			}

			// 전체 패킷이 아직 도착하지 않았으면 대기
			if (available < static_cast<int32>(totalSize))
				break;

			// 패킷 단위로 ECS에 전달
			shared_ptr<RecvBuffer> pkt = RecvBuffer::FromSpan(base, totalSize);
			const entt::entity e = GetEntity();
			if (e != entt::null)
			{
				ProcessReceivedPacket(e, pkt);
			}

			processed += static_cast<int32>(totalSize);
		}

		// 소비
		if (processed < 0 || m_streamBuffer.DataSize() < processed || !m_streamBuffer.OnRead(processed))
		{
			Disconnect();
			JAMNET_LOG_WARN_LOC("[TcpSession] OnRead Overflow");
			return;
		}
		m_streamBuffer.Clean();
	}

	//	부분전송 지원- 남은 구간을 재등록, 다 보냈으면 정리. 
	void TcpSession::ProcessSend(SendEvent* ev,  int32 numOfBytes)
	{
		if (!ev) return;

		if (numOfBytes <= 0)
		{
			ev->sendBuffers.clear();
			ev->gather.clear();
			ObjectPool<SendEvent>::Push(ev);
			Disconnect();
			JAMNET_LOG_WARN_LOC("[TcpSession] Send 0 byte");
			return;
		}

		// 상위 통지(이번 완료 바이트)
		OnSend(numOfBytes);

		// 남은 바이트 계산
		uint32 remaining = 0;
		if (numOfBytes < static_cast<int32>(ev->totalBytes))
			remaining = ev->totalBytes - static_cast<uint32>(numOfBytes);

		if (remaining == 0)
		{
			ev->sendBuffers.clear();
			ev->gather.clear();
			ObjectPool<SendEvent>::Push(ev);
			return;
		}

		// curIndex/curOffset 갱신
		uint32 advance = static_cast<uint32>(numOfBytes);
		size_t idx = ev->curIndex;
		ULONG off = ev->curOffset;

		while (advance > 0 && idx < ev->gather.size())
		{
			const ULONG leftInBuf = ev->gather[idx].len - off;
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
		ev->curIndex = idx;
		ev->curOffset = off;
		ev->totalBytes = remaining;

		// 다음 WSABUF 배열 구성(첫 요소 offset 적용)
		WSABUF first{};
		std::vector<WSABUF> bufs;
		if (ev->curIndex < ev->gather.size())
		{
			first = ev->gather[ev->curIndex];
			first.buf += ev->curOffset;
			first.len -= ev->curOffset;
			first.len -= ev->curOffset;

			bufs.reserve(ev->gather.size() - ev->curIndex);
			bufs.push_back(first);
			for (size_t i = ev->curIndex + 1; i < ev->gather.size(); ++i)
				bufs.push_back(ev->gather[i]);
		}

		if (bufs.empty())
		{
			ev->sendBuffers.clear();
			ev->gather.clear();
			ObjectPool<SendEvent>::Push(ev);
			return;
		}

		DWORD sent = 0;
		if (SOCKET_ERROR == ::WSASend(m_socket, bufs.data(), static_cast<DWORD>(bufs.size()), OUT & sent, 0, ev, nullptr))
		{
			const int32 ec = ::WSAGetLastError();
			if (ec != WSA_IO_PENDING)
			{
				HandleError(ec);
				ev->sendBuffers.clear();
				ev->gather.clear();
				ObjectPool<SendEvent>::Push(ev);
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
