#include "pch.h"
#include "TcpSession.h"

namespace jam::net
{
	/*-----------------
		TcpSession
	------------------*/

	TcpSession::TcpSession() : m_recvBuffer(BUFFER_SIZE)
	{
		m_socket = SocketUtils::CreateSocket(EProtocolType::TCP);
	}

	TcpSession::~TcpSession()
	{
		SocketUtils::Close(m_socket);
	}

	bool TcpSession::Connect()
	{
		return RegisterConnect();
	}

	void TcpSession::Disconnect(const WCHAR* cause)
	{
		if (m_connected.exchange(false) == false)
			return;

		RegisterDisconnect();
	}

	void TcpSession::Send(Sptr<SendBuffer> sendBuffer)
	{
		if (IsConnected() == false)
			return;

		bool registerSend = false;

		{
			WRITE_LOCK;
			m_sendQueue.push(sendBuffer);

			if (m_sendRegistered.exchange(true) == false)
				registerSend = true;
		}

		if (registerSend)
			RegisterSend();
	}

	HANDLE TcpSession::GetHandle()
	{
		return reinterpret_cast<HANDLE>(m_socket);
	}

	void TcpSession::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
	{
		switch (iocpEvent->m_eventType)
		{
		case EventType::Connect:
			ProcessConnect();
			break;
		case EventType::Disconnect:
			ProcessDisconnect();
			break;
		case EventType::Recv:
			ProcessRecv(numOfBytes);
			break;
		case EventType::Send:
			ProcessSend(numOfBytes);
			break;
		}
	}

	bool TcpSession::RegisterConnect()
	{
		if (IsConnected()) return false;

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
		if (IsConnected() == false)
			return;

		m_recvEvent.Init();
		m_recvEvent.m_owner = shared_from_this();

		WSABUF wsaBuf;
		wsaBuf.buf = reinterpret_cast<char*>(m_recvBuffer.WritePos());
		wsaBuf.len = m_recvBuffer.FreeSize();

		DWORD numOfBytes = 0;
		DWORD flags = 0;
		if (SOCKET_ERROR == ::WSARecv(m_socket, &wsaBuf, 1, OUT &numOfBytes, OUT &flags, &m_recvEvent, nullptr))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				HandleError(errorCode);
				m_recvEvent.m_owner = nullptr;
			}
		}
	}

	void TcpSession::RegisterSend()
	{
		if (IsConnected() == false)
			return;

		m_sendEvent.Init();
		m_sendEvent.m_owner = shared_from_this();

		{
			WRITE_LOCK

			int32 writeSize = 0;
			while (m_sendQueue.empty() == false)
			{
				Sptr<SendBuffer> sendBuffer = m_sendQueue.front();

				writeSize += sendBuffer->WriteSize();
				// TODO: exception check

				m_sendQueue.pop();
				m_sendEvent.sendBuffers.push_back(sendBuffer);
			}
		}

		// Scatter-Gather
		xvector<WSABUF> wsaBufs;
		wsaBufs.reserve(m_sendEvent.sendBuffers.size());
		for (const Sptr<SendBuffer>& sendBuffer : m_sendEvent.sendBuffers)
		{
			WSABUF wsaBuf;
			wsaBuf.buf = reinterpret_cast<char*>(sendBuffer->Buffer());
			wsaBuf.len = static_cast<LONG>(sendBuffer->WriteSize());
			wsaBufs.push_back(wsaBuf);
		}

		DWORD numOfBytes = 0;
		if (SOCKET_ERROR == ::WSASend(m_socket, wsaBufs.data(), static_cast<DWORD>(wsaBufs.size()), OUT & numOfBytes, 0, &m_sendEvent, nullptr))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				HandleError(errorCode);
				m_sendEvent.m_owner = nullptr;
				m_sendEvent.sendBuffers.clear();
				m_sendRegistered.store(false);
			}
		}
	}

	void TcpSession::ProcessConnect()
	{
		m_connectEvent.m_owner = nullptr;
		m_connected.store(true);

		GetService()->AddTcpSession(static_pointer_cast<TcpSession>(shared_from_this()));
		OnConnected();
		RegisterRecv();
	}

	void TcpSession::ProcessDisconnect()
	{
		m_disconnectEvent.m_owner = nullptr;

		OnDisconnected();
		GetService()->ReleaseTcpSession(static_pointer_cast<TcpSession>(shared_from_this()));
	}

	void TcpSession::ProcessRecv(int32 numOfBytes)
	{
		m_recvEvent.m_owner = nullptr;

		if (numOfBytes == 0)
		{
			Disconnect(L"Receive 0 byte");
			return;
		}

		if (m_recvBuffer.OnWrite(numOfBytes) == false)
		{
			Disconnect(L"OnWrite Overflow");
			return;
		}

		const int32 dataSize = m_recvBuffer.DataSize();
		const int32 processLen = IsParsingPacket(m_recvBuffer.ReadPos(), dataSize);

		if (processLen < 0 || dataSize < processLen || m_recvBuffer.OnRead(processLen) == false)
		{
			Disconnect(L"OnRead Overflow");
			return;
		}

		m_recvBuffer.Clean();

		RegisterRecv();
	}

	void TcpSession::ProcessSend(int32 numOfBytes)
	{
		m_sendEvent.m_owner = nullptr;
		m_sendEvent.sendBuffers.clear();

		if (numOfBytes == 0)
		{
			Disconnect(L"Send 0 byte");
			return;
		}

		OnSend(numOfBytes);

		WRITE_LOCK
		if (m_sendQueue.empty())
			m_sendRegistered.store(false);
		else
			RegisterSend();
	}

	int32 TcpSession::IsParsingPacket(BYTE* buffer, const int32 len)
	{
		int32 processLen = 0;

		while (true)
		{
			int32 dataSize = len - processLen;

			if (dataSize < sizeof(TcpPacketHeader))
				break;

			TcpPacketHeader* header = reinterpret_cast<TcpPacketHeader*>(&buffer[processLen]);

			if (dataSize < header->size || header->size < sizeof(TcpPacketHeader))
				break;

			OnRecv(&buffer[processLen], header->size);

			processLen += header->size;
		}

		return processLen;
	}

	void TcpSession::HandleError(int32 errorCode)
	{
		switch (errorCode)
		{
		case WSAECONNRESET:
		case WSAECONNABORTED:
			Disconnect(L"Handle Error");
			break;
		default:
			cout << "Handle Error : " << errorCode << '\n';
			break;
		}
	}
}
