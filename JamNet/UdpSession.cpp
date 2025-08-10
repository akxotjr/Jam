#include "pch.h"
#include "UdpSession.h"
#include "Clock.h"
#include "RpcManager.h"
#include "FragmentHandler.h"
#include "CongestionController.h"
#include "NetStat.h"
#include "PacketBuilder.h"
#include "ReliableTransportManager.h"



namespace jam::net
{
	UdpSession::UdpSession() : m_recvBuffer(BUFFER_SIZE)
	{
		m_sid = GenerateSID(eProtocolType::UDP);

		m_netStatTracker = std::make_unique<NetStatTracker>();
		m_congestionController = std::make_unique<CongestionController>();
		m_fragmentHandler = std::make_unique<FragmentHandler>();
		m_reliableTransportManager = std::make_unique<ReliableTransportManager>();
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

	void UdpSession::Send(const Sptr<SendBuffer>& buf)
	{
		if (!m_reliableTransportManager->TryAttachPiggybackAck(buf))
		{
			uint64 now = Clock::Instance().GetCurrentTick();
			if (m_reliableTransportManager->ShouldSendImmediateAck(now))
			{
				SendAck();
				m_reliableTransportManager->ClearPendingAck();
			}
			// else ?
		}

		SendDirect(buf);
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

		int32 processLen = ParsePacket(buf, totalSize);
		if (processLen < 0 || totalSize < processLen || !m_recvBuffer.OnRead(processLen)) return;
		m_recvBuffer.Clean();
	}

	int32 UdpSession::ParsePacket(BYTE* buffer, int32 len)
	{
		if (len < static_cast<int32>(sizeof(PacketHeader)))
			return 0;

		PacketBuilder pb;
		pb.BeginRead(buffer, len);

		PacketHeader pktHeader;
		pb.DetachHeaders(pktHeader);

		int32 size = GetPacketSize(pktHeader.sizeAndflags);
		if (len < size || size < sizeof(PacketHeader))
			return 0;

		uint8 flags = GetPacketFlags(pktHeader.sizeAndflags);

		// Check if the packet is a reliable packet
		if (flags & FLAG_RELIABLE)
		{
			RudpHeader rudpHeader;
			pb.DetachHeaders(rudpHeader);
			if (!m_reliableTransportManager->IsSeqReceived(rudpHeader.sequence))
			{
				m_reliableTransportManager->AddPendingPacket(rudpHeader.sequence, pb.GetSendBuffer(), Clock::Instance().GetCurrentTick());
				SendAck(rudpHeader.sequence);
			}
		}


		switch (pktHeader.type)
		{
		case ePacketType::SYSTEM:
			{
			HandleSystemPacket(buffer, len, pb);
			break;
			}
		case ePacketType::RPC:
			{
			HandleRpcPacket(buffer, len, pb);
			break;
			}
		case ePacketType::ACK:
			{
			HandleAckPacket(buffer, len, pb);
			break;
			}
		case ePacketType::CUSTOM:
			break;
		default:
			break;
		}

		pb.EndRead();

		return size;
	}

	void UdpSession::HandleSystemPacket(BYTE* buf, uint32 size, PacketBuilder& pb/*SysHeader* sys, BYTE* payload, uint32 payloadLen*/)
	{
		SysHeader sys;
		pb.DetachHeaders(sys);

		switch (sys.sysId)
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
			{
			PING payload;
			uint32 payloadSize;
			pb.DetachPayload(&payload, payloadSize);
			OnRecvPing(payload);
			break;
			}
		case eSysPacketId::S_PONG:
			{
			PONG payload;
			uint32 payloadSize;
			pb.DetachPayload(&payload, payloadSize);
			OnRecvPong(payload);
			break;
			}
		default:
			break;
		}
	}

	void UdpSession::HandleRpcPacket(BYTE* buffer, uint32 size, PacketBuilder& pb/*RpcHeader* rpc, BYTE* payload, uint32 payloadLen*/)
	{
		RpcHeader rpcHeader;
		pb.DetachHeaders(rpcHeader);

		BYTE* payload;
		uint32 payloadSize;
		pb.DetachPayload(payload, payloadSize);

		m_rpcManager->Dispatch(GetSession(), rpcHeader.rpcId, rpcHeader.requestId, rpcHeader.flags, payload, payloadSize);
	}

	void UdpSession::HandleAckPacket(BYTE* buffer, uint32 size, PacketBuilder& pb)
	{
		AckHeader ackHeader;
		pb.DetachHeaders(ackHeader);

		m_reliableTransportManager->OnRecvAck(ackHeader.latestSeq, ackHeader.bitfield);
		m_netStatTracker->OnRecvAck(ackHeader.latestSeq);
	}

	void UdpSession::HandleCustomPacket(BYTE* data, uint32 len)
	{
		//todo
	}

	void UdpSession::UpdateRetry()
	{
		const uint64 now = Clock::Instance().GetCurrentTick();

		if (!IsConnected())
			CheckRetryHandshake(now);

	//	CheckRetrySend(now);
	}


	//void UdpSession::CheckRetryHandshake(uint64 now)
	//{
	//	if (m_handshakeState == eHandshakeState::SYN_SENT)
	//	{
	//		if (now - m_lastHandshakeTime > HANDSHAKE_RETRY_INTERVAL)
	//		{
	//			if (m_handshakeRetryCount >= MAX_HANDSHAKE_RETRIES)
	//			{
	//				std::cout << "[Handshake] Timeout. Disconnecting.\n";
	//				Disconnect(L"Handshake timeout");
	//				return;
	//			}

	//			std::cout << "[Handshake] Retrying SYN...\n";
	//			SendHandshakeSyn();
	//			m_handshakeRetryCount++;
	//			m_lastHandshakeTime = now;
	//		}
	//	}

	//	if (m_handshakeState == eHandshakeState::SYNACK_SENT)
	//	{
	//		if (now - m_lastHandshakeTime > HANDSHAKE_RETRY_INTERVAL * MAX_HANDSHAKE_RETRIES)
	//		{
	//			std::cout << "[Server] Handshake ACK timeout. Releasing session.\n";
	//			Disconnect(L"Handshake ACK timeout");
	//			return;
	//		}
	//	}
	//}

	//void UdpSession::CheckRetrySend(uint64 now)
	//{
	//	xvector<uint16> resendList;
	//	int32 lostPackets = 0;

	//	{
	//		WRITE_LOCK

	//		for (auto& [seq, pkt] : m_pendingAckMap)
	//		{
	//			uint64 elapsed = now - pkt.timestamp;

	//			if (elapsed >= m_resendIntervalMs)
	//			{
	//				pkt.timestamp = now;
	//				pkt.retryCount++;

	//				resendList.push_back(seq);
	//				lostPackets++;
	//			}

	//			if (pkt.retryCount > 5)
	//			{
	//				std::cout << "[ReliableUDP] Max retry reached. Disconnecting.\n";
	//				m_congestionController->OnPacketLoss();
	//				m_netStatTracker->OnPacketLoss();
	//				Disconnect(L"Too many retries");
	//				continue;
	//			}
	//		}
	//	}

	//	if (lostPackets > 0)
	//	{
	//		m_netStatTracker->OnPacketLoss(lostPackets);
	//	}

	//	for (uint16 seq : resendList)
	//	{
	//		auto it = m_pendingAckMap.find(seq);
	//		if (it != m_pendingAckMap.end())
	//		{
	//			std::cout << "[ReliableUDP] Re-sending seq: " << seq << "\n";
	//			SendReliable(it->second.buffer);
	//		}
	//	}
	//}

	//void UdpSession::HandleAck(uint16 latestSeq, uint32 bitfield)
	//{
	//	//WRITE_LOCK
	//	const uint64 now = Clock::Instance().GetCurrentTick();

	//	for (uint16 i = 0; i <= BITFIELD_SIZE; ++i)
	//	{
	//		uint16 ackSeq = latestSeq - i;
	//		if (i == 0 || (bitfield & (1 << (i - 1))))
	//		{
	//			auto it = m_pendingAckMap.find(ackSeq);
	//			if (it != m_pendingAckMap.end())
	//			{
	//				uint64 sendTick = it->second.timestamp;
	//				float rtt = static_cast<float>(now - sendTick);

	//				m_congestionController->OnRecvAck(rtt);
	//				m_netStatTracker->OnRecvAck(ackSeq);


	//				WRITE_LOCK
	//				m_pendingAckMap.erase(it);
	//			}
	//		}
	//	}
	//}

	//bool UdpSession::CheckAndRecordReceiveHistory(uint16 seq)
	//{
	//	if (!IsSeqGreater(seq, m_latestSeq - WINDOW_SIZE))
	//		return false;

	//	if (m_receiveHistory.test(seq % WINDOW_SIZE))
	//		return false;

	//	m_receiveHistory.set(seq % WINDOW_SIZE);
	//	m_latestSeq = IsSeqGreater(seq, m_latestSeq) ? seq : m_latestSeq;
	//	return true;
	//}

	//uint32 UdpSession::GenerateAckBitfield(uint16 latestSeq)
	//{
	//	uint32 bitfield = 0;
	//	for (uint16 i = 1; i <= BITFIELD_SIZE; ++i)
	//	{
	//		uint16 seq = latestSeq - i;

	//		if (!IsSeqGreater(latestSeq, seq))
	//			continue;

	//		if (m_receiveHistory.test(seq % WINDOW_SIZE))
	//		{
	//			bitfield |= (1 << (i - 1));
	//		}
	//	}
	//	return bitfield;
	//}

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



	//void UdpSession::SendHandshakeSyn()
	//{
	//	cout << "UdpSession::SendHandshakeSyn()\n";
	//	if (m_state != eSessionState::DISCONNECTED || m_handshakeState != eHandshakeState::NONE)
	//		return;

	//	m_state = eSessionState::HANDSHAKING;
	//	m_handshakeState = eHandshakeState::SYN_SENT;

	//	auto buf = MakeHandshakePkt(eSysPacketId::C_HANDSHAKE_SYN);
	//	SendDirect(buf);
	//}

	//void UdpSession::OnRecvHandshakeSynAck()
	//{
	//	cout << "UdpSession::OnRecvHandshakeSynAck()\n";
	//	if (m_state != eSessionState::HANDSHAKING || m_handshakeState != eHandshakeState::SYN_SENT)
	//		return;

	//	m_handshakeState = eHandshakeState::SYNACK_RECV;
	//	SendHandshakeAck();
	//}

	//void UdpSession::SendHandshakeAck()
	//{
	//	cout << "UdpSession::SendHandshakeAck()\n";
	//	if (m_state != eSessionState::HANDSHAKING || m_handshakeState != eHandshakeState::SYNACK_RECV)
	//		return;

	//	auto buf = MakeHandshakePkt(eSysPacketId::C_HANDSHAKE_ACK);
	//	SendDirect(buf);

	//	m_handshakeState = eHandshakeState::COMPLETE;
	//	m_state = eSessionState::CONNECTED;

	//	GetService()->CompleteUdpHandshake(GetRemoteNetAddress());
	//	OnConnected();
	//}

	////-------------------------------------------------------------------


	//void UdpSession::OnRecvHandshakeSyn()
	//{
	//	cout << "UdpSession::OnRecvHandshakeSyn()\n";

	//	if (m_state != eSessionState::DISCONNECTED || m_handshakeState != eHandshakeState::NONE)
	//		return;

	//	m_state = eSessionState::HANDSHAKING;
	//	m_handshakeState = eHandshakeState::SYN_RECV;

	//	SendHandshakeSynAck();
	//}

	//void UdpSession::SendHandshakeSynAck()
	//{
	//	cout << "UdpSession::SendHandshakeSynAck()\n";
	//	if (m_state != eSessionState::HANDSHAKING || m_handshakeState != eHandshakeState::SYN_RECV)
	//		return;

	//	m_handshakeState = eHandshakeState::SYNACK_SENT;
	//	
	//	auto buf = MakeHandshakePkt(eSysPacketId::S_HANDSHAKE_SYNACK);
	//	SendDirect(buf);
	//}

	//void UdpSession::OnRecvHandshakeAck()
	//{
	//	cout << "UdpSession::OnRecvHandshakeAck()\n";

	//	if (m_state != eSessionState::HANDSHAKING || m_handshakeState != eHandshakeState::SYNACK_SENT)
	//		return;

	//	m_handshakeState = eHandshakeState::COMPLETE;
	//	m_state = eSessionState::CONNECTED;

	//	GetService()->CompleteUdpHandshake(GetRemoteNetAddress());
	//	OnConnected();
	//}

	//Sptr<SendBuffer> UdpSession::MakeHandshakePkt(eSysPacketId id)
	//{
	//	constexpr uint16 size = sizeof(PacketHeader) + sizeof(SysHeader);

	//	PacketBuilder pb;
	//	pb.BeginWrite(size);

	//	PacketHeader pktHeader = {
	//		.sizeAndflags = MakeSizeAndFlags(size, 0),
	//		.type = static_cast<uint8>(ePacketType::SYSTEM)
	//	};

	//	SysHeader sysHeader = { .sysId = static_cast<uint8>(id) };

	//	pb.AttachHeaders(pktHeader, sysHeader);
	//	//pb.Finalize();
	//	pb.EndWrite();

	//	return pb.GetSendBuffer();
	//}


	/*Sptr<SendBuffer> UdpSession::MakeAckPkt()
	{
		constexpr uint16 size = sizeof(PacketHeader) + sizeof(AckHeader);

		PacketBuilder pb;

		pb.BeginWrite(size);

		PacketHeader pktHeader = {
			.sizeAndflags = MakeSizeAndFlags(size, 0),
			.type = static_cast<uint8>(ePacketType::SYSTEM)
		};

		uint16 seq = m_reliableTransportManager->GetPendigAckSeq();
		uint32 bitfield = m_reliableTransportManager->GetPendingAckBitfield();

		AckHeader ackHeader = {
			.latestSeq = seq,
			.bitfield = bitfield
		};

		pb.AttachHeaders(pktHeader, ackHeader);
		pb.EndWrite();

		return pb.GetSendBuffer();
	}*/

	void UdpSession::SendAck()
	{
		auto buf = MakeAckPkt();

		m_netStatTracker->OnSendReliablePacket();
		SendDirect(buf);
	}


	void UdpSession::OnRecvAppData(BYTE* data, uint32 len)
	{
		m_netStatTracker->OnRecv(len);
		OnRecv(data, len);
	}

	//void UdpSession::SendPing()
	//{
	//	constexpr uint16 size = sizeof(PacketHeader) + sizeof(RudpHeader) + sizeof(SysHeader) + sizeof(PING);

	//	PacketBuilder pb;
	//	pb.BeginWrite(size);

	//	PacketHeader pktHeader = {
	//		.sizeAndflags = MakeSizeAndFlags(size, 0),
	//		.type = static_cast<uint8>(ePacketType::SYSTEM)
	//	};

	//	RudpHeader rudpHeader = {
	//		.sequence = m_reliableTransportManager->GetNextSendSeq(),
	//	};

	//	SysHeader sysHeader = {
	//		.sysId = static_cast<uint8>(eSysPacketId::C_PING)
	//	};

	//	PING payload = {
	//		.clientSendTick = Clock::Instance().GetCurrentTick()
	//	};

	//	pb.AttachHeaders(pktHeader, rudpHeader, sysHeader);
	//	pb.AttachPayload(&payload, sizeof(PING));
	//	//pb.Finalize();
	//	pb.EndWrite();
	//	
	//	SendDirect(pb.GetSendBuffer());
	//}

	//void UdpSession::SendPong(uint64 clientSendTick)
	//{
	//	constexpr uint16 size = sizeof(PacketHeader) + sizeof(RudpHeader) + sizeof(SysHeader) + sizeof(PONG);

	//	PacketBuilder pb;
	//	pb.BeginWrite(size);

	//	PacketHeader pktHeader = {
	//		.sizeAndflags = MakeSizeAndFlags(size, 0),
	//		.type = static_cast<uint8>(ePacketType::SYSTEM)
	//	};

	//	RudpHeader rudpHeader = {
	//		.sequence = m_reliableTransportManager->GetNextSendSeq(),
	//	};

	//	SysHeader sysHeader = {
	//		.sysId = static_cast<uint8>(eSysPacketId::S_PONG)
	//	};

	//	PONG payload = {
	//		.clientSendTick = clientSendTick,
	//		.serverSendTick = Clock::Instance().GetCurrentTick()
	//	};

	//	pb.AttachHeaders(pktHeader, rudpHeader, sysHeader);
	//	pb.AttachPayload(&payload, sizeof(PONG));

	//	//pb.Finalize();
	//	pb.EndWrite();

	//	SendDirect(pb.GetSendBuffer());
	//}

	void UdpSession::OnRecvPing(PING ping)
	{
		const uint64 serverRecvTick = Clock::Instance().GetCurrentTick();
		m_netStatTracker->OnRecvPing(ping.clientSendTick, serverRecvTick);

		SendPong(ping.clientSendTick);
	}

	void UdpSession::OnRecvPong(PONG pong)
	{
		const uint64 clientRecvTick = Clock::Instance().GetCurrentTick();

		m_netStatTracker->OnRecvPong(pong.clientSendTick, clientRecvTick, pong.serverSendTick);
	}

	void UdpSession::SendDirect(const Sptr<SendBuffer>& buf)
	{
		GetService()->m_udpRouter->RegisterSend(buf, GetRemoteNetAddress());
	}
}
