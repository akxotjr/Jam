#include "pch.h"
#include "TcpSession.h"

namespace jam::net
{
	/*-----------------
		TcpSession
	------------------*/

	TcpSession::TcpSession() : _recvBuffer(BUFFER_SIZE)
	{
		_socket = SocketUtils::CreateSocket(ProtocolType::PROTOCOL_TCP);
	}

	TcpSession::~TcpSession()
	{
		SocketUtils::Close(_socket);
	}

	bool TcpSession::Connect()
	{
		return RegisterConnect();
	}

	void TcpSession::Disconnect(const WCHAR* cause)
	{
		if (_connected.exchange(false) == false)
			return;

		RegisterDisconnect();
	}

	void TcpSession::Send(SendBufferRef sendBuffer)
	{
		if (IsConnected() == false)
			return;

		bool registerSend = false;

		{
			WRITE_LOCK;
			_sendQueue.push(sendBuffer);

			if (_sendRegistered.exchange(true) == false)
				registerSend = true;
		}

		if (registerSend)
			RegisterSend();
	}

	HANDLE TcpSession::GetHandle()
	{
		return reinterpret_cast<HANDLE>(_socket);
	}

	void TcpSession::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
	{
		switch (iocpEvent->eventType)
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

		if (SocketUtils::SetReuseAddress(_socket, true) == false) return false;

		if (SocketUtils::BindAnyAddress(_socket, 0) == false) return false;

		_connectEvent.Init();
		_connectEvent.owner = shared_from_this();

		DWORD numOfBytes = 0;
		SOCKADDR_IN sockAddr = GetService()->GetTcpNetAddress().GetSockAddr();
		if (SOCKET_ERROR == SocketUtils::ConnectEx(_socket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr), nullptr, 0, &numOfBytes, &_connectEvent))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				_connectEvent.owner = nullptr;
				return false;
			}
		}
		return true;
	}

	bool TcpSession::RegisterDisconnect()
	{
		_disconnectEvent.Init();
		_disconnectEvent.owner = shared_from_this();

		if (false == SocketUtils::DisconnectEx(_socket, &_disconnectEvent, TF_REUSE_SOCKET, 0))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				_disconnectEvent.owner = nullptr;
				return false;
			}
		}
		return true;
	}

	void TcpSession::RegisterRecv()
	{
		if (IsConnected() == false)
			return;

		_recvEvent.Init();
		_recvEvent.owner = shared_from_this();

		WSABUF wsaBuf;
		wsaBuf.buf = reinterpret_cast<char*>(_recvBuffer.WritePos());
		wsaBuf.len = _recvBuffer.FreeSize();

		DWORD numOfBytes = 0;
		DWORD flags = 0;
		if (SOCKET_ERROR == ::WSARecv(_socket, &wsaBuf, 1, OUT & numOfBytes, OUT & flags, &_recvEvent, nullptr))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				HandleError(errorCode);
				_recvEvent.owner = nullptr;
			}
		}
	}

	void TcpSession::RegisterSend()
	{
		if (IsConnected() == false)
			return;

		_sendEvent.Init();
		_sendEvent.owner = shared_from_this();

		{
			WRITE_LOCK;

			int32 writeSize = 0;
			while (_sendQueue.empty() == false)
			{
				SendBufferRef sendBuffer = _sendQueue.front();

				writeSize += sendBuffer->WriteSize();
				// TODO: exception check

				_sendQueue.pop();
				_sendEvent.sendBuffers.push_back(sendBuffer);
			}
		}

		// Scatter-Gather
		Vector<WSABUF> wsaBufs;
		wsaBufs.reserve(_sendEvent.sendBuffers.size());
		for (const SendBufferRef& sendBuffer : _sendEvent.sendBuffers)
		{
			WSABUF wsaBuf;
			wsaBuf.buf = reinterpret_cast<char*>(sendBuffer->Buffer());
			wsaBuf.len = static_cast<LONG>(sendBuffer->WriteSize());
			wsaBufs.push_back(wsaBuf);
		}

		DWORD numOfBytes = 0;
		if (SOCKET_ERROR == ::WSASend(_socket, wsaBufs.data(), static_cast<DWORD>(wsaBufs.size()), OUT & numOfBytes, 0, &_sendEvent, nullptr))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				HandleError(errorCode);
				_sendEvent.owner = nullptr;
				_sendEvent.sendBuffers.clear();
				_sendRegistered.store(false);
			}
		}
	}

	void TcpSession::ProcessConnect()
	{
		_connectEvent.owner = nullptr;
		_connected.store(true);

		GetService()->AddTcpSession(static_pointer_cast<TcpSession>(shared_from_this()));
		OnConnected();
		RegisterRecv();
	}

	void TcpSession::ProcessDisconnect()
	{
		_disconnectEvent.owner = nullptr;

		OnDisconnected();
		GetService()->ReleaseTcpSession(static_pointer_cast<TcpSession>(shared_from_this()));
	}

	void TcpSession::ProcessRecv(int32 numOfBytes)
	{
		_recvEvent.owner = nullptr;

		if (numOfBytes == 0)
		{
			Disconnect(L"Receive 0 byte");
			return;
		}

		if (_recvBuffer.OnWrite(numOfBytes) == false)
		{
			Disconnect(L"OnWrite Overflow");
			return;
		}

		const int32 dataSize = _recvBuffer.DataSize();
		const int32 processLen = IsParsingPacket(_recvBuffer.ReadPos(), dataSize);

		if (processLen < 0 || dataSize < processLen || _recvBuffer.OnRead(processLen) == false)
		{
			Disconnect(L"OnRead Overflow");
			return;
		}

		_recvBuffer.Clean();

		RegisterRecv();
	}

	void TcpSession::ProcessSend(int32 numOfBytes)
	{
		_sendEvent.owner = nullptr;
		_sendEvent.sendBuffers.clear();

		if (numOfBytes == 0)
		{
			Disconnect(L"Send 0 byte");
			return;
		}

		OnSend(numOfBytes);

		WRITE_LOCK
			if (_sendQueue.empty())
				_sendRegistered.store(false);
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
