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
		//RegisterConnect();
		//ProcessConnect();
		// temp
		return true;
	}

	void UdpSession::Disconnect(const WCHAR* cause)
	{
		if (_connected.exchange(false) == false)
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
			WRITE_LOCK;
			_pendingAckMap[seq] = pkt;
		}

		Send(sendBuffer);
	}



	HANDLE UdpSession::GetHandle()
	{
		return reinterpret_cast<HANDLE>(GetService()->GetUdpSocket());
	}

	void UdpSession::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
	{
		if (iocpEvent->m_eventType != EventType::Send)
			return;

		ProcessSend(numOfBytes);
	}

	void UdpSession::RegisterSend(Sptr<SendBuffer> sendBuffer)
	{
		if (IsConnected() == false)
			return;

		_sendEvent.Init();
		_sendEvent.m_owner = shared_from_this();

		WSABUF wsaBuf;
		wsaBuf.buf = reinterpret_cast<char*>(sendBuffer->Buffer());
		wsaBuf.len = static_cast<ULONG>(sendBuffer->WriteSize());

		DWORD numOfBytes = 0;
		SOCKADDR_IN remoteAddr = GetRemoteNetAddress().GetSockAddr();

		if (SOCKET_ERROR == ::WSASendTo(GetService()->GetUdpSocket(), &wsaBuf, 1, OUT & numOfBytes, 0, reinterpret_cast<SOCKADDR*>(&remoteAddr), sizeof(SOCKADDR_IN), &_sendEvent, nullptr))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				cout << "Handle Error : " << errorCode << '\n';
				HandleError(errorCode);
				//_sendEvent.owner = nullptr;
				_sendEvent.sendBuffers.clear();
			}
		}
	}

	void UdpSession::ProcessConnect()
	{
		_connected.store(true);
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
		//_sendEvent.owner = nullptr;
		_sendEvent.sendBuffers.clear();

		if (numOfBytes == 0)
		{
			Disconnect(L"Send 0 byte");
			return;
		}

		OnSend(numOfBytes);
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
