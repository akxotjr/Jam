#include "pch.h"
#include "UdpSession.h"

namespace jam::net
{

	UdpSession::UdpSession()
	{
	}

	UdpSession::~UdpSession()
	{
	}

	bool UdpSession::Connect()
	{
		return RegisterConnect();
	}

	void UdpSession::Disconnect(const WCHAR* cause)
	{
		if (m_connected.exchange(false) == false)
			return;

		ProcessDisconnect();
	}

	void UdpSession::Send(Sptr<SendBuffer> sendBuffer)
	{
		if (IsConnected() == false)
			return;

		RegisterSend(sendBuffer);
	}

	void UdpSession::SendReliable(Sptr<SendBuffer> sendBuffer, double timestamp)
	{
		uint16 seq = _sendSeq++;

		UdpPacketHeader* header = reinterpret_cast<UdpPacketHeader*>(sendBuffer->Buffer());
		header->sequence = seq;

		PendingPacket pkt = { .buffer = sendBuffer, .sequence = seq, .timestamp = timestamp, .retryCount = 0 };

		{
			WRITE_LOCK
			_pendingAckMap[seq] = pkt;
		}

		Send(sendBuffer);
	}

	//HANDLE UdpSession::GetHandle()
	//{
	//	return reinterpret_cast<HANDLE>(GetService()->GetUdpSocket());
	//}

	//void UdpSession::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
	//{
	//	if (iocpEvent->m_eventType != EventType::Send)
	//		return;

	//	ProcessSend(numOfBytes);
	//}

	void UdpSession::RegisterSend(Sptr<SendBuffer> sendBuffer)
	{
		//if (IsConnected() == false)
		//	return;

		//m_sendEvent.Init();
		//m_sendEvent.m_owner = shared_from_this();

		//WSABUF wsaBuf;
		//wsaBuf.buf = reinterpret_cast<char*>(sendBuffer->Buffer());
		//wsaBuf.len = static_cast<ULONG>(sendBuffer->WriteSize());

		//DWORD numOfBytes = 0;
		//SOCKADDR_IN remoteAddr = GetRemoteNetAddress().GetSockAddr();

		//if (SOCKET_ERROR == ::WSASendTo(GetService()->GetUdpSocket(), &wsaBuf, 1, OUT &numOfBytes, 0, reinterpret_cast<SOCKADDR*>(&remoteAddr), sizeof(SOCKADDR_IN), &m_sendEvent, nullptr))
		//{
		//	const int32 errorCode = ::WSAGetLastError();
		//	if (errorCode != WSA_IO_PENDING)
		//	{
		//		cout << "Handle Error : " << errorCode << '\n';
		//		HandleError(errorCode);
		//		//_sendEvent.owner = nullptr;
		//		m_sendEvent.sendBuffers.clear();
		//	}
		//}

		GetService()->m_udpRouter->RegisterSend(sendBuffer, GetRemoteNetAddress());
	}

	void UdpSession::RegisterRecv()
	{
	}

	void UdpSession::ProcessConnect()
	{
		m_connected.store(true);
		GetService()->CompleteUdpHandshake(GetRemoteNetAddress());
		OnConnected();
	}

	void UdpSession::ProcessDisconnect()
	{
		OnDisconnected();
		GetService()->ReleaseUdpSession(static_pointer_cast<UdpSession>(shared_from_this()));
	}

	void UdpSession::ProcessSend(int32 numOfBytes)
	{
		//m_sendEvent.sendBuffers.clear();

		//if (numOfBytes == 0)
		//{
		//	Disconnect(L"Send 0 byte");
		//	return;
		//}

		OnSend(numOfBytes);
	}

	void UdpSession::ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer)
	{
		m_recvBuffer = recvBuffer;
		if (m_recvBuffer.OnWrite(numOfBytes) == false)
		{
			std::cout << "[ProcessRecv] OnWrite failed! FreeSize: " << m_recvBuffer.FreeSize() << ", numOfBytes: " << numOfBytes << "\n";
			return;
		}

		BYTE* buf = m_recvBuffer.ReadPos();
		if (!buf)
		{
			std::cout << "[ProcessRecv] buffer is null\n";
			return;
		}

		int32 dataSize = m_recvBuffer.DataSize();
		int32 processLen = IsParsingPacket(buf, dataSize);

		if (processLen < 0 || dataSize < processLen || m_recvBuffer.OnRead(processLen) == false)
		{
			std::cout << "[ProcessRecv] Invalid processLen: " << processLen << ", dataSize: " << dataSize << "\n";
			return;
		}

		m_recvBuffer.Clean();
		RegisterRecv();
	}

	int32 UdpSession::IsParsingPacket(BYTE* buffer, const int32 len)
	{
		int32 processLen = 0;

		while (true)
		{
			int32 dataSize = len - processLen;

			if (dataSize < sizeof(UdpPacketHeader))
				break;

			UdpPacketHeader header = *reinterpret_cast<UdpPacketHeader*>(&buffer[processLen]);

			if (dataSize < header.size || header.size < sizeof(UdpPacketHeader))
				break;

			if (processLen + header.size > len)
				break;

			OnRecv(&buffer[0], header.size);

			processLen += header.size;
		}

		return processLen;
	}

	void UdpSession::ProcessHandshake()
	{

	}

	void UdpSession::Update(double serverTime)
	{
		xvector<uint16> resendList;

		{
			WRITE_LOCK

			for (auto& [seq, pkt] : _pendingAckMap)
			{
				double elapsed = serverTime - pkt.timestamp;

				if (elapsed >= _resendIntervalMs)
				{
					pkt.timestamp = serverTime;
					pkt.retryCount++;

					resendList.push_back(seq);
				}

				if (pkt.retryCount > 5)
				{
					std::cout << "[ReliableUDP] Max retry reached. Disconnecting.\n";
					Disconnect(L"Too many retries");
					continue;
				}
			}
		}

		for (uint16 seq : resendList)
		{
			auto it = _pendingAckMap.find(seq);
			if (it != _pendingAckMap.end())
			{
				std::cout << "[ReliableUDP] Re-sending seq: " << seq << "\n";
				SendReliable(it->second.buffer, serverTime);
			}
		}
	}

	void UdpSession::HandleAck(uint16 latestSeq, uint32 bitfield)
	{
		WRITE_LOCK

		for (int32 i = 0; i <= BITFIELD_SIZE; ++i)
		{
			uint16 ackSeq = latestSeq - i;

			if (i == 0 || (bitfield & (1 << (i - 1))))
			{
				auto it = _pendingAckMap.find(ackSeq);
				if (it != _pendingAckMap.end())
				{
					_pendingAckMap.erase(it);
				}
			}
		}
	}

	bool UdpSession::CheckAndRecordReceiveHistory(uint16 seq)
	{
		if (!IsSeqGreater(seq, _latestSeq - WINDOW_SIZE))
			return false;

		if (_receiveHistory.test(seq % WINDOW_SIZE))
			return false;

		_receiveHistory.set(seq % WINDOW_SIZE);
		_latestSeq = IsSeqGreater(seq, _latestSeq) ? seq : _latestSeq;
		return true;
	}

	uint32 UdpSession::GenerateAckBitfield(uint16 latestSeq)
	{
		uint32 bitfield = 0;
		for (int32 i = 1; i <= BITFIELD_SIZE; ++i)
		{
			uint16 seq = latestSeq - i;

			if (!IsSeqGreater(latestSeq, seq))
				continue;

			if (_receiveHistory.test(seq % WINDOW_SIZE))
			{
				bitfield |= (1 << (i - 1));
			}
		}
		return bitfield;
	}

	void UdpSession::HandleError(int32 errorCode)
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
