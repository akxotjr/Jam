#include "pch.h"
#include "UdpSession.h"
#include "BufferReader.h"
#include "BufferWriter.h"
#include "Clock.h"
#include "RpcManager.h"


namespace jam::net
{
	UdpSession::UdpSession() : m_recvBuffer(BUFFER_SIZE)
	{
		m_sid = GenerateSID(eProtocolType::UDP);

		m_netStatTracker = std::make_unique<NetStatTracker>();
		m_congestionController = std::make_unique<CongestionController>();
	}

	UdpSession::~UdpSession()
	{
	}

	bool UdpSession::Connect()
	{
		if (IsConnected())
			return false;

		SendHandshakeSyn();
		return true;
	}

	void UdpSession::Disconnect(const WCHAR* cause)
	{
		if (IsConnected() == false)
			return;

		ProcessDisconnect();
	}

	void UdpSession::Send(const Sptr<SendBuffer>& sendBuffer)
	{
		GetService()->m_udpRouter->RegisterSend(sendBuffer, GetRemoteNetAddress());
	}

	void UdpSession::SendReliable(const Sptr<SendBuffer>& sendBuffer)
	{
		size_t inFlightBytes = m_pendingAckMap.size() * sendBuffer->WriteSize();
		if (!m_congestionController->CanSend(inFlightBytes))
			return;

		uint16 seq = m_sendSeq++;

		UdpPacketHeader* header = reinterpret_cast<UdpPacketHeader*>(sendBuffer->Buffer());
		header->sequence = seq;

		uint64 timestamp = Clock::Instance().GetCurrentTick();

		PendingPacket pkt = { .buffer = sendBuffer, .sequence = seq, .timestamp = timestamp, .retryCount = 0 };

		{
			WRITE_LOCK
			m_pendingAckMap[seq] = pkt;
		}

		m_netStatTracker->OnSend(sendBuffer->WriteSize());
		Send(sendBuffer);
	}


	//void UdpSession::ProcessConnect()
	//{
	//	m_connected.store(true);
	//	GetService()->CompleteUdpHandshake(GetRemoteNetAddress());
	//	OnConnected();
	//}

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
		//int32 processed = ParseAndDispatchPackets(buf, totalSize);
		int32 processLen = ParsePacket(buf, totalSize);
		if (processLen < 0 || totalSize < processLen || !m_recvBuffer.OnRead(processLen)) return;
		m_recvBuffer.Clean();
	}

	int32 UdpSession::ParsePacket(BYTE* buffer, int32 len)
	{
		if (len < static_cast<int32>(sizeof(PacketHeader)))
			return 0;

		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
		if (len < header->size || header->size < sizeof(UdpPacketHeader))
			return 0;


		switch (header->type)
		{
		case ePacketType::SYSTEM:
			{
				SysHeader* sys = reinterpret_cast<SysHeader*>(header + sizeof(PacketHeader));
				BYTE* payload = reinterpret_cast<BYTE*>(sys + 1);
				uint32 payloadLen = len - sizeof(PacketHeader) - sizeof(SysHeader);
				HandleSystemPacket(sys, payload, payloadLen);
				break;
			}
		case ePacketType::RPC:
			{
				RpcHeader* rpc = reinterpret_cast<RpcHeader*>(header + sizeof(PacketHeader));
				BYTE* payload = reinterpret_cast<BYTE*>(rpc + 1);
				uint32 payloadLen = len - sizeof(PacketHeader) - sizeof(RpcHeader);
				HandleRpcPacket(rpc, payload, payloadLen);
				break;
			}
		case ePacketType::ACK:
			{
				AckHeader* ack = reinterpret_cast<AckHeader*>(header + sizeof(PacketHeader));
				HandleAckPacket(ack);
				break;
			}
		case ePacketType::CUSTOM:
			{
				BYTE* data = reinterpret_cast<BYTE*>(header + sizeof(PacketHeader));
				uint32 length = len - sizeof(PacketHeader);
				HandleCustomPacket(data, length);
				break;
			}
		default:
			return 0;
		}

		return header->size;
	}

	void UdpSession::HandleSystemPacket(SysHeader* sys, BYTE* payload, uint32 payloadLen)
	{
		switch (sys->sysId)
		{
		case eSysPacketId::C_HANDSHAKE_SYN:
			OnRecvHandshakeSyn();
			break;
		case eSysPacketId::S_HANDSHAKE_SYNACK:
			OnRecvHandshakeSynAck();
			break;
		case eSysPacketId::C_HANDSHAKE_ACK:
			OnRecvHandshakeAck();
			break;
		case eSysPacketId::C_PING:
			OnRecvPing(payload, payloadLen);
			break;
		case eSysPacketId::S_PONG:
			OnRecvPong(payload, payloadLen);
			break;
		default:
			break;
		}
	}

	void UdpSession::HandleRpcPacket(RpcHeader* rpc, BYTE* payload, uint32 payloadLen)
	{
		RpcManager::Instance().Dispatch(GetSession(), rpc->rpcId, rpc->requestId, rpc->flags, payload, payloadLen);
	}

	void UdpSession::HandleAckPacket(AckHeader* ack)
	{
		const uint16 latestSeq = ack->latestSeq;
		const uint32 bitfield = ack->bitfield;

		HandleAck(latestSeq, bitfield);
	}

	void UdpSession::HandleCustomPacket(BYTE* data, uint32 len)
	{
	}


	//int32 UdpSession::IsParsingPacket(BYTE* buffer, const int32 len)
	//{
	//	int32 processLen = 0;

	//	while (true)
	//	{
	//		int32 dataSize = len - processLen;

	//		if (dataSize < sizeof(UdpPacketHeader))
	//			break;

	//		UdpPacketHeader header = *reinterpret_cast<UdpPacketHeader*>(&buffer[processLen]);

	//		if (dataSize < header.size || header.size < sizeof(UdpPacketHeader))
	//			break;

	//		if (processLen + header.size > len)
	//			break;

	//		OnRecv(&buffer[0], header.size);

	//		processLen += header.size;
	//	}

	//	return processLen;
	//}

	//int32 UdpSession::ParseAndDispatchPackets(BYTE* buffer, int32 len)
	//{
	//	int32 processed = 0;
	//	while (processed < len)
	//	{
	//		int32 remain = len - processed;
	//		if (remain < sizeof(UdpPacketHeader)) break;

	//		UdpPacketHeader* header = reinterpret_cast<UdpPacketHeader*>(buffer + processed);
	//		if (remain < header->size || header->size < sizeof(UdpPacketHeader)) break;

	//		DispatchPacket(header, header->size);
	//		processed += header->size;
	//	}
	//	return processed;
	//}

	//void UdpSession::DispatchPacket(UdpPacketHeader* header, uint32 len)
	//{
	//	switch (static_cast<eRudpPacketId>(header->id))
	//	{
	//	case eRudpPacketId::ACK:
	//		OnRecvAck(reinterpret_cast<BYTE*>(header), len);
	//		break;
	//	case eRudpPacketId::C_HANDSHAKE_SYN:
	//	case eRudpPacketId::S_HANDSHAKE_SYNACK:
	//	case eRudpPacketId::C_HANDSHAKE_ACK:
	//		ProcessHandshake(header);
	//		break;
	//	case eRudpPacketId::C_PING:
	//		OnRecvPing(reinterpret_cast<BYTE*>(header), len);
	//		break;
	//	case eRudpPacketId::S_PONG:
	//		OnRecvPong(reinterpret_cast<BYTE*>(header), len);
	//		break;
	//	default:
	//		if (header->sequence > 0 && CheckAndRecordReceiveHistory(header->sequence))
	//		{
	//			SendAck(header->sequence);
	//			OnRecvAppData(reinterpret_cast<BYTE*>(header), len);
	//		}
	//		else
	//		{
	//			OnRecvAppData(reinterpret_cast<BYTE*>(header), len);
	//		}
	//		break;
	//	}
	//}

	//void UdpSession::ProcessHandshake(UdpPacketHeader* header)
	//{
	//	switch (static_cast<eRudpPacketId>(header->id))
	//	{
	//	case eRudpPacketId::C_HANDSHAKE_SYN:
	//		OnRecvHandshakeSyn();
	//		break;
	//	case eRudpPacketId::S_HANDSHAKE_SYNACK:
	//		OnRecvHandshakeSynAck();
	//		break;
	//	case eRudpPacketId::C_HANDSHAKE_ACK:
	//		OnRecvHandshakeAck();
	//		break;
	//	default:
	//		break;
	//	}
	//}

	void UdpSession::UpdateRetry()
	{
		const uint64 now = Clock::Instance().GetCurrentTick();

		if (!IsConnected())
			CheckRetryHandshake(now);

		CheckRetrySend(now);
	}


	void UdpSession::CheckRetryHandshake(uint64 now)
	{
		if (m_handshakeState == eHandshakeState::SYN_SENT)
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

		if (m_handshakeState == eHandshakeState::SYNACK_SENT)
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
		int32 lostPackets = 0;

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
					lostPackets++;
				}

				if (pkt.retryCount > 5)
				{
					std::cout << "[ReliableUDP] Max retry reached. Disconnecting.\n";
					m_congestionController->OnPacketLoss();
					m_netStatTracker->OnPacketLoss();
					Disconnect(L"Too many retries");
					continue;
				}
			}
		}

		if (lostPackets > 0)
		{
			m_netStatTracker->OnPacketLoss(lostPackets);
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
		//WRITE_LOCK
		const uint64 now = Clock::Instance().GetCurrentTick();

		for (uint16 i = 0; i <= BITFIELD_SIZE; ++i)
		{
			uint16 ackSeq = latestSeq - i;
			if (i == 0 || (bitfield & (1 << (i - 1))))
			{
				auto it = m_pendingAckMap.find(ackSeq);
				if (it != m_pendingAckMap.end())
				{
					uint64 sendTick = it->second.timestamp;
					float rtt = static_cast<float>(now - sendTick);

					m_congestionController->OnRecvAck(rtt);
					m_netStatTracker->OnRecvAck(ackSeq);


					WRITE_LOCK
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
		for (uint16 i = 1; i <= BITFIELD_SIZE; ++i)
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
		if (m_state != eSessionState::DISCONNECTED || m_handshakeState != eHandshakeState::NONE)
			return;

		m_state = eSessionState::HANDSHAKING;
		m_handshakeState = eHandshakeState::SYN_SENT;

		auto buf = MakeHandshakePkt(eRudpPacketId::C_HANDSHAKE_SYN);
		Send(buf);
	}

	void UdpSession::OnRecvHandshakeSynAck()
	{
		cout << "UdpSession::OnRecvHandshakeSynAck()\n";
		if (m_state != eSessionState::HANDSHAKING || m_handshakeState != eHandshakeState::SYN_SENT)
			return;

		m_handshakeState = eHandshakeState::SYNACK_RECV;
		SendHandshakeAck();
	}

	void UdpSession::SendHandshakeAck()
	{
		cout << "UdpSession::SendHandshakeAck()\n";
		if (m_state != eSessionState::HANDSHAKING || m_handshakeState != eHandshakeState::SYNACK_RECV)
			return;

		auto buf = MakeHandshakePkt(eRudpPacketId::C_HANDSHAKE_ACK);
		Send(buf);

		m_handshakeState = eHandshakeState::COMPLETE;
		m_state = eSessionState::CONNECTED;

		GetService()->CompleteUdpHandshake(GetRemoteNetAddress());
		OnConnected();
	}

	//-------------------------------------------------------------------


	void UdpSession::OnRecvHandshakeSyn()
	{
		cout << "UdpSession::OnRecvHandshakeSyn()\n";

		if (m_state != eSessionState::DISCONNECTED || m_handshakeState != eHandshakeState::NONE)
			return;

		m_state = eSessionState::HANDSHAKING;
		m_handshakeState = eHandshakeState::SYN_RECV;

		SendHandshakeSynAck();
	}

	void UdpSession::SendHandshakeSynAck()
	{
		cout << "UdpSession::SendHandshakeSynAck()\n";
		if (m_state != eSessionState::HANDSHAKING || m_handshakeState != eHandshakeState::SYN_RECV)
			return;

		m_handshakeState = eHandshakeState::SYNACK_SENT;
		
		auto buf = MakeHandshakePkt(eRudpPacketId::S_HANDSHAKE_SYNACK);
		Send(buf);
	}

	void UdpSession::OnRecvHandshakeAck()
	{
		cout << "UdpSession::OnRecvHandshakeAck()\n";

		if (m_state != eSessionState::HANDSHAKING || m_handshakeState != eHandshakeState::SYNACK_SENT)
			return;

		m_handshakeState = eHandshakeState::COMPLETE;
		m_state = eSessionState::CONNECTED;

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
		constexpr uint16 pktSize = sizeof(UdpPacketHeader) + sizeof(AckPacket);
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
		m_netStatTracker->OnRecv(len);
		OnRecv(data, len);
	}

	void UdpSession::SendPing()
	{
		constexpr uint16 pktSize = sizeof(UdpPacketHeader) + sizeof(uint64);
		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(pktSize);

		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		UdpPacketHeader* header = bw.Reserve<UdpPacketHeader>();
		header->id = static_cast<uint16>(eRudpPacketId::C_PING);
		header->size = pktSize;
		header->sequence = m_sendSeq;

		const uint64 clientSendTick = Clock::Instance().GetCurrentTick();
		bw << clientSendTick;
		buf->Close(bw.WriteSize());

		Send(buf);
	}

	void UdpSession::SendPong(uint64 clientSendTick)
	{
		constexpr uint16 pktSize = sizeof(UdpPacketHeader) + sizeof(uint64) + sizeof(uint64);
		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(pktSize);

		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		UdpPacketHeader* header = bw.Reserve<UdpPacketHeader>();
		header->id = static_cast<uint16>(eRudpPacketId::S_PONG);
		header->size = pktSize;
		header->sequence = m_sendSeq;

		const uint64 serverTick = Clock::Instance().GetCurrentTick();

		bw << clientSendTick << serverTick;

		buf->Close(bw.WriteSize());

		Send(buf);
	}

	void UdpSession::OnRecvPing(BYTE* payload, uint32 payloadLen)
	{
		BufferReader br(payload, payloadLen);

		uint64 clientSendTick;
		br >> clientSendTick;

		const uint64 serverTick = Clock::Instance().GetCurrentTick();
		m_netStatTracker->OnRecvPing(clientSendTick, serverTick);

		SendPong(clientSendTick);
	}

	void UdpSession::OnRecvPong(BYTE* payload, uint32 payloadLen)
	{
		BufferReader br(payload, payloadLen);

		uint64 clientSendTick;
		uint64 serverTick;
		br >> clientSendTick >> serverTick;

		const uint64 clientRecvTick = Clock::Instance().GetCurrentTick();

		m_netStatTracker->OnRecvPong(clientSendTick, clientRecvTick, serverTick);
	}
}
