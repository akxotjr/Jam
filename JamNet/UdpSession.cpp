#include "pch.h"
#include "UdpSession.h"
#include "BufferReader.h"
#include "BufferWriter.h"


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
		GetService()->m_udpRouter->RegisterSend(sendBuffer, GetRemoteNetAddress());
	}

	void UdpSession::SendReliable(Sptr<SendBuffer> sendBuffer)
	{
		uint16 seq = m_sendSeq++;

		UdpPacketHeader* header = reinterpret_cast<UdpPacketHeader*>(sendBuffer->Buffer());
		header->sequence = seq;

		uint64 timestamp = ::GetTickCount64();

		PendingPacket pkt = { .buffer = sendBuffer, .sequence = seq, .timestamp = timestamp, .retryCount = 0 };

		{
			WRITE_LOCK
			m_pendingAckMap[seq] = pkt;
		}

		Send(sendBuffer);
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
		OnSend(numOfBytes);
	}

	void UdpSession::ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer)
	{
		m_recvBuffer = recvBuffer;
		if (!m_recvBuffer.OnWrite(numOfBytes)) return;
		BYTE* buf = m_recvBuffer.ReadPos();
		int32 totalSize = m_recvBuffer.DataSize();
		int32 processed = ParseAndDispatchPackets(buf, totalSize);
		if (processed < 0 || totalSize < processed || !m_recvBuffer.OnRead(processed)) return;
		m_recvBuffer.Clean();
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

	int32 UdpSession::ParseAndDispatchPackets(BYTE* buffer, int32 len)
	{
		int32 processed = 0;
		while (processed < len)
		{
			int32 remain = len - processed;
			if (remain < sizeof(UdpPacketHeader)) break;

			UdpPacketHeader* header = reinterpret_cast<UdpPacketHeader*>(buffer + processed);
			if (remain < header->size || header->size < sizeof(UdpPacketHeader)) break;

			DispatchPacket(header, header->size);
			processed += header->size;
		}
		return processed;
	}

	void UdpSession::DispatchPacket(UdpPacketHeader* header, uint32 len)
	{
		switch (static_cast<eRudpPacketId>(header->id))
		{
		case eRudpPacketId::ACK:
			OnRecvAck(reinterpret_cast<BYTE*>(header), len);
			break;
		case eRudpPacketId::C_HANDSHAKE_SYN:
		case eRudpPacketId::S_HANDSHAKE_SYNACK:
		case eRudpPacketId::C_HANDSHAKE_ACK:
			ProcessHandshake(header);
			break;
		default:
			if (header->sequence > 0 && CheckAndRecordReceiveHistory(header->sequence))
			{
				SendAck(header->sequence);
				OnRecvAppData(reinterpret_cast<BYTE*>(header), len);
			}
			else
			{
				OnRecvAppData(reinterpret_cast<BYTE*>(header), len);
			}
			break;
		}
	}

	void UdpSession::ProcessHandshake(UdpPacketHeader* header)
	{
		switch (static_cast<eRudpPacketId>(header->id))
		{
		case eRudpPacketId::C_HANDSHAKE_SYN:
			OnRecvHandshakeSyn();
			break;
		case eRudpPacketId::S_HANDSHAKE_SYNACK:
			OnRecvHandshakeSynAck();
			break;
		case eRudpPacketId::C_HANDSHAKE_ACK:
			OnRecvHandshakeAck();
			break;
		default:
			break;
		}
	}

	void UdpSession::UpdateRetry()
	{
		const uint64 now = ::GetTickCount64();

		if (!IsConnected())
			CheckRetryHandshake(now);

		CheckRetrySend(now);
	}


	void UdpSession::CheckRetryHandshake(uint64 now)
	{
		if (m_state == eUdpSessionState::SynSent)
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

		if (m_state == eUdpSessionState::SynAckSent)
		{
			if (now - m_lastHandshakeTime > HANDSHAKE_RETRY_INTERVAL * MAX_HANDSHAKE_RETRIES)
			{
				std::cout << "[Server] Handshake ACK timeout. Releasing session.\n";
				Disconnect(L"Handshake ACK timeout");
				return;
			}
		}
	}

	void UdpSession::CheckRetrySend(uint64 now)
	{
		xvector<uint16> resendList;

		{
			WRITE_LOCK

			for (auto& [seq, pkt] : m_pendingAckMap)
			{
				uint64 elapsed = now - pkt.timestamp;

				if (elapsed >= m_resendIntervalMs)
				{
					pkt.timestamp = now;
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
			auto it = m_pendingAckMap.find(seq);
			if (it != m_pendingAckMap.end())
			{
				std::cout << "[ReliableUDP] Re-sending seq: " << seq << "\n";
				SendReliable(it->second.buffer);
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
				auto it = m_pendingAckMap.find(ackSeq);
				if (it != m_pendingAckMap.end())
				{
					m_pendingAckMap.erase(it);
				}
			}
		}
	}

	bool UdpSession::CheckAndRecordReceiveHistory(uint16 seq)
	{
		if (!IsSeqGreater(seq, m_latestSeq - WINDOW_SIZE))
			return false;

		if (m_receiveHistory.test(seq % WINDOW_SIZE))
			return false;

		m_receiveHistory.set(seq % WINDOW_SIZE);
		m_latestSeq = IsSeqGreater(seq, m_latestSeq) ? seq : m_latestSeq;
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

			if (m_receiveHistory.test(seq % WINDOW_SIZE))
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
		if (m_state != eUdpSessionState::Disconnected)
			return;

		m_state = eUdpSessionState::SynSent;

		auto buf = MakeHandshakePkt(eRudpPacketId::C_HANDSHAKE_SYN);
		Send(buf);
	}

	void UdpSession::OnRecvHandshakeSynAck()
	{
		cout << "UdpSession::OnRecvHandshakeSynAck()\n";
		if (m_state != eUdpSessionState::SynSent)
			return;

		m_state = eUdpSessionState::SynAckReceived;
		SendHandshakeAck();
	}

	void UdpSession::SendHandshakeAck()
	{
		cout << "UdpSession::SendHandshakeAck()\n";
		if (m_state != eUdpSessionState::SynAckReceived)
			return;

		auto buf = MakeHandshakePkt(eRudpPacketId::C_HANDSHAKE_ACK);
		Send(buf);

		m_state = eUdpSessionState::Connected;
		m_connected.store(true);

		GetService()->CompleteUdpHandshake(GetRemoteNetAddress());

		OnConnected();
	}

	//-------------------------------------------------------------------


	void UdpSession::OnRecvHandshakeSyn()
	{
		cout << "UdpSession::OnRecvHandshakeSyn()\n";

		if (m_state != eUdpSessionState::Disconnected)
			return;

		m_state = eUdpSessionState::SynReceived;
		SendHandshakeSynAck();
	}

	void UdpSession::SendHandshakeSynAck()
	{
		cout << "UdpSession::SendHandshakeSynAck()\n";

		if (m_state != eUdpSessionState::SynReceived)
			return;

		m_state = eUdpSessionState::SynAckSent;
		
		auto buf = MakeHandshakePkt(eRudpPacketId::S_HANDSHAKE_SYNACK);
		Send(buf);
	}

	void UdpSession::OnRecvHandshakeAck()
	{
		cout << "UdpSession::OnRecvHandshakeAck()\n";

		if (m_state != eUdpSessionState::SynAckSent)
			return;

		m_state = eUdpSessionState::Connected;
		m_connected.store(true);

		GetService()->CompleteUdpHandshake(GetRemoteNetAddress());

		OnConnected();
	}

	Sptr<SendBuffer> UdpSession::MakeHandshakePkt(eRudpPacketId id)
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


	Sptr<SendBuffer> UdpSession::MakeAckPkt(uint16 seq)
	{
		const uint16 pktSize = sizeof(UdpPacketHeader) + sizeof(AckPacket);
		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(pktSize);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		UdpPacketHeader* header = bw.Reserve<UdpPacketHeader>();
		header->id = static_cast<uint16>(eRudpPacketId::ACK);
		header->size = pktSize;
		header->sequence = seq;

		bw << seq << GenerateAckBitfield(seq);

		buf->Close(bw.WriteSize());
		return buf;
	}

	void UdpSession::SendAck(uint16 seq)
	{
		Send(MakeAckPkt(seq));
	}


	void UdpSession::OnRecvAck(BYTE* data, uint32 len)
	{
		BufferReader br(data, len);

		UdpPacketHeader header;
		br >> header;

		uint16 latestSeq;
		uint32 bitfield;
		br >> latestSeq >> bitfield;

		HandleAck(latestSeq, bitfield);
	}


	void UdpSession::OnRecvAppData(BYTE* data, uint32 len)
	{
		// 응용 계층 패킷 처리
		OnRecv(data, len);
	}


}
