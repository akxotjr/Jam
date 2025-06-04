#include "pch.h"
#include "UdpSession.h"

namespace jam::net
{

	UdpSession::UdpSession() : m_recvBuffer(BUFFER_SIZE)
	{
	}

	UdpSession::~UdpSession()
	{
	}

	bool UdpSession::Connect()
	{
		SendHandshakeSyn();
		return true;
	}

	void UdpSession::Disconnect(const WCHAR* cause)
	{
		if (m_connected.exchange(false) == false)
			return;

		ProcessDisconnect();
	}

	void UdpSession::Send(Sptr<SendBuffer> sendBuffer)
	{
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

	void UdpSession::RegisterSend(Sptr<SendBuffer> sendBuffer)
	{
		wstring temp = GetRemoteNetAddress().GetIpAddress();
		

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

	void UdpSession::ProcessHandshake(int32 numOfBytes, RecvBuffer& recvBuffer)
	{
		if (IsConnected())
			return;

		m_recvBuffer = recvBuffer;
		if (m_recvBuffer.OnWrite(numOfBytes) == false)
			return;

		BYTE* buf = m_recvBuffer.ReadPos();
		if (!buf)
		{
			std::cout << "[ProcessRecv] buffer is null\n";
			return;
		}

		int32 dataSize = m_recvBuffer.DataSize();
		if (dataSize != sizeof(UdpPacketHeader))
			return;

		m_recvBuffer.OnRead(dataSize);

		UdpPacketHeader* header = reinterpret_cast<UdpPacketHeader*>(buf);

		cout << "pktid : " << header->id << endl;

		switch (static_cast<HandshakePacketId>(header->id))
		{
		case HandshakePacketId::C_HANDSHAKE_SYN:
			OnRecvHandshakeSyn();
			break;
		case HandshakePacketId::S_HANDSHAKE_SYNACK:
			OnRecvHandshakeSynAck();
			break;
		case HandshakePacketId::C_HANDSHAKE_ACK:
			OnRecvHandshakeAck();
			break;
		default:
			break;
		}
	}

	void UdpSession::Update(double serverTime)
	{
		if (!IsConnected())
			CheckRetryHandshake();

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

	void UdpSession::CheckRetryHandshake()
	{
		double now = utils::TimeManager::Instance().GetCurrentTime();

		if (m_state == EUdpSessionState::SynSent)
		{
			if (now - m_lastHandshakeTime > HANDSHAKE_RETRY_INTERVAL)
			{
				if (m_handshakeRetryCount >= MAX_HANDSHAKE_RETRIES)
				{
					std::cout << "[Handshake] Timeout. Disconnecting.\n";
					Disconnect(L"Handshake timeout");
					return;
				}

				std::cout << "[Handshake] Retrying SYN...\n";
				SendHandshakeSyn();
				m_handshakeRetryCount++;
				m_lastHandshakeTime = now;
			}
		}

		if (m_state == EUdpSessionState::SynAckSent)
		{
			if (now - m_lastHandshakeTime > HANDSHAKE_RETRY_INTERVAL * MAX_HANDSHAKE_RETRIES)
			{
				std::cout << "[Server] Handshake ACK timeout. Releasing session.\n";
				Disconnect(L"Handshake ACK timeout");
				return;
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

	HANDLE UdpSession::GetHandle()
	{
		return HANDLE();
	}

	void UdpSession::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
	{
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








	void UdpSession::SendHandshakeSyn()
	{
		cout << "UdpSession::SendHandshakeSyn()\n";
		if (m_state != EUdpSessionState::Disconnected)
			return;

		m_state = EUdpSessionState::SynSent;

		auto buf = MakeHandshakePkt(HandshakePacketId::C_HANDSHAKE_SYN);
		Send(buf);
	}

	void UdpSession::OnRecvHandshakeSynAck()
	{
		cout << "UdpSession::OnRecvHandshakeSynAck()\n";
		if (m_state != EUdpSessionState::SynSent)
			return;

		m_state = EUdpSessionState::SynAckReceived;
		SendHandshakeAck();
	}

	void UdpSession::SendHandshakeAck()
	{
		cout << "UdpSession::SendHandshakeAck()\n";
		if (m_state != EUdpSessionState::SynAckReceived)
			return;

		auto buf = MakeHandshakePkt(HandshakePacketId::C_HANDSHAKE_ACK);
		Send(buf);

		m_state = EUdpSessionState::Connected;
		m_connected.store(true);
		OnConnected();
	}

	//-------------------------------------------------------------------


	void UdpSession::OnRecvHandshakeSyn()
	{
		cout << "UdpSession::OnRecvHandshakeSyn()\n";

		if (m_state != EUdpSessionState::Disconnected)
			return;

		m_state = EUdpSessionState::SynReceived;
		SendHandshakeSynAck();
	}

	void UdpSession::SendHandshakeSynAck()
	{
		cout << "UdpSession::SendHandshakeSynAck()\n";

		if (m_state != EUdpSessionState::SynReceived)
			return;

		m_state = EUdpSessionState::SynAckSent;
		
		auto buf = MakeHandshakePkt(HandshakePacketId::S_HANDSHAKE_SYNACK);
		Send(buf);
	}

	void UdpSession::OnRecvHandshakeAck()
	{
		cout << "UdpSession::OnRecvHandshakeAck()\n";

		if (m_state != EUdpSessionState::SynAckSent)
			return;

		m_state = EUdpSessionState::Connected;
		m_connected.store(true);
		OnConnected();
	}

	Sptr<SendBuffer> UdpSession::MakeHandshakePkt(HandshakePacketId id)
	{
		const uint16 pktId = static_cast<uint16>(id);
		const uint16 pktSize = static_cast<uint16>(sizeof(UdpPacketHeader));

		Sptr<SendBuffer> sendBuffer = SendBufferManager::Instance().Open(pktSize);
		UdpPacketHeader* header = reinterpret_cast<UdpPacketHeader*>(sendBuffer->Buffer());

		header->id = pktId;
		header->size = pktSize;

		sendBuffer->Close(pktSize);

		return sendBuffer;
	}
}
