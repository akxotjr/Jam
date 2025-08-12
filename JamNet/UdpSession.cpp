#include "pch.h"
#include "UdpSession.h"
#include "Clock.h"
#include "RpcManager.h"
#include "FragmentHandler.h"
#include "CongestionController.h"
#include "NetStat.h"
#include "PacketBuilder.h"
#include "ReliableTransportManager.h"
#include "HandshakeManager.h"
#include "PacketBuilder.h"


namespace jam::net
{
	UdpSession::UdpSession() : m_recvBuffer(BUFFER_SIZE)
	{
		m_sid = GenerateSID(eProtocolType::UDP);

		m_handshakeManager = std::make_unique<HandshakeManager>(this);
		m_netStatTracker = std::make_unique<NetStatTracker>();
		m_congestionController = std::make_unique<CongestionController>();
		m_fragmentHandler = std::make_unique<FragmentHandler>();
		m_reliableTransportManager = std::make_unique<ReliableTransportManager>();
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


	void UdpSession::SendDirect(const Sptr<SendBuffer>& buf)
	{
		GetService()->m_udpRouter->RegisterSend(buf, GetRemoteNetAddress());
	}
}
