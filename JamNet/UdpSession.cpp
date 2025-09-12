#include "pch.h"
#include "UdpSession.h"

//#include "ChannelManager.h"
#include "Clock.h"
#include "RpcManager.h"
//#include "FragmentManager.h"
//#include "CongestionController.h"
//#include "NetStatManager.h"
#include "PacketBuilder.h"
//#include "ReliableTransportManager.h"
//#include "HandshakeManager.h"


namespace jam::net
{
	UdpSession::UdpSession() : m_recvBuffer(BUFFER_SIZE)
	{
		m_sid = GenerateSID(eProtocolType::UDP);

		//m_handshakeManager				= std::make_unique<HandshakeManager>(this);
		//m_netStatTracker				= std::make_unique<NetStatManager>();
		//m_congestionController			= std::make_unique<CongestionController>(this);
		//m_fragmentManager				= std::make_unique<FragmentManager>(this);
		//m_reliableTransportManager		= std::make_unique<ReliableTransportManager>(this);
		//m_channelManager				= std::make_unique<ChannelManager>(this);
	}


	bool UdpSession::Connect()
	{
		if (IsConnected())
			return false;

		auto self = static_pointer_cast<UdpSession>(shared_from_this());

		m_endpoint->EmitConnect();

		//Post(utils::job::Job([this]()
		//	{
		//		this->m_state = eSessionState::HANDSHAKING;
		//		
		//	}));

		//m_state = eSessionState::HANDSHAKING;
		//m_handshakeManager->InitiateConnection();
		return true;
	}

	void UdpSession::Disconnect(const WCHAR* cause)
	{
		//if (IsConnected() == false)
		//	return;

		//Post(utils::job::Job([this]()
		//	{
		//		this->m_state = eSessionState::HANDSHAKING;
		//		
		//	}));
		//m_state = eSessionState::HANDSHAKING;
		//m_handshakeManager->InitiateDisconnection();

		m_endpoint->EmitDisconnect();
	}

	void UdpSession::Send(const Sptr<SendBuffer>& buf)
	{
		if (!buf || !buf->Buffer())
			return;

		//auto self = static_pointer_cast<UdpSession>(shared_from_this());
		//self->Post(utils::job::Job([self, buf] {
		//		self->ProcessSend(buf);
		//	}));

		m_endpoint->EmitSend();
	}

	void UdpSession::Update()
	{
		//auto self = static_pointer_cast<UdpSession>(shared_from_this());
		//self->Post(utils::job::Job([self] {
		//		self->ProcessUpdate();
		//	}));
	}

	void UdpSession::OnLinkEstablished()
	{
		GetService()->CompleteUdpHandshake(m_remoteAddress);
		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		self->PostCtrl(utils::job::Job([self] {
				//self->m_state = eSessionState::CONNECTED;
				self->OnConnected();
			}));
	}

	void UdpSession::OnLinkTerminated()
	{
		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		self->PostCtrl(utils::job::Job([self] {
				//self->m_state = eSessionState::DISCONNECTED;
				self->OnDisconnected();
			}));
		GetService()->ReleaseUdpSession(static_pointer_cast<UdpSession>(shared_from_this()));
	}



	void UdpSession::ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer)
	{
		m_recvBuffer = recvBuffer;
		if (!m_recvBuffer.OnWrite(numOfBytes)) return;
		BYTE* buf = m_recvBuffer.ReadPos();
		int32 totalSize = m_recvBuffer.DataSize();

		uint32 processLen = ParsePacket(buf, totalSize);
		if (processLen < 0 || totalSize < processLen || !m_recvBuffer.OnRead(processLen)) return;
		m_recvBuffer.Clean();
	}

	uint32 UdpSession::ParsePacket(BYTE* buf, uint32 size)
	{


		/*PacketAnalysis analysis = PacketBuilder::AnalyzePacket(buf, size);

		if (!analysis.isValid)
			return 0;

		if (analysis.IsReliable())
		{
			uint16 seq = analysis.GetSequence();
			if (!m_reliableTransportManager->IsSeqReceived(seq))
			{
				return analysis.totalSize;
			}
		}

		if (analysis.IsFragmented())
		{
			auto fragmentResult = m_fragmentManager->OnRecvFragment(buf, size);

			if (fragmentResult.first)
			{
				ProcessReassembledPayload(fragmentResult.second, analysis);
			}

			return analysis.totalSize;
		}

		BYTE* payload = analysis.GetPayloadPtr(buf);
		uint32 payloadSize = analysis.payloadSize;

		auto self = static_pointer_cast<UdpSession>(shared_from_this()); 
		const uint8 id = analysis.GetId();

		switch (analysis.GetType())
		{
		case ePacketType::SYSTEM: 
		{
			xvector<BYTE> data(payload, payload + payloadSize);
			self->PostCtrl(utils::job::Job([self, id, data = std::move(data)]() mutable {
					self->HandleSystemPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		case ePacketType::ACK: 
		{
			xvector<BYTE> data(payload, payload + payloadSize);
			self->PostCtrl(utils::job::Job([self, id, data = std::move(data)]() mutable {
					self->HandleAckPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		case ePacketType::RPC: 
		{
			xvector<BYTE> data(payload, payload + payloadSize);
			self->Post(utils::job::Job([self, id, data = std::move(data)]() mutable {
					self->HandleRpcPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		case ePacketType::CUSTOM: 
		{
			xvector<BYTE> data(payload, payload + payloadSize);
			self->Post(utils::job::Job([self, id, data = std::move(data)]() mutable {
					self->HandleCustomPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		}

		return analysis.totalSize;*/
	}

	void UdpSession::HandleSystemPacket(uint8 id, BYTE* payload, uint32 payloadSize)
	{
		switch (id)
		{
		case eSystemPacketId::CONNECT_SYN:
		case eSystemPacketId::CONNECT_SYNACK:
		case eSystemPacketId::CONNECT_ACK:
			m_handshakeManager->HandleConnectionPacket(U2E(eSystemPacketId, id));
			break;
		case eSystemPacketId::DISCONNECT_FIN:
		case eSystemPacketId::DISCONNECT_FINACK:
		case eSystemPacketId::DISCONNECT_ACK:
			m_handshakeManager->HandleDisconnectionPacket(U2E(eSystemPacketId, id));
			break;
		case eSystemPacketId::PING:
			if (payloadSize >= sizeof(PING)) 
			{
				PING* pingData = reinterpret_cast<PING*>(payload);
				m_netStatTracker->OnRecvPing(pingData->clientSendTick);
			}
			break;
		case eSystemPacketId::PONG:
			if (payloadSize >= sizeof(PING))
			{
				PING* pongData = reinterpret_cast<PING*>(payload);
				m_netStatTracker->OnRecvPong(pongData->clientSendTick, );
			}
			break;
		default:
			break;
		}
	}

	void UdpSession::HandleRpcPacket(uint8 id, BYTE* payload, uint32 payloadSize)
	{
		switch (static_cast<eRpcPacketId>(id))
		{
		case eRpcPacketId::FLATBUFFER_RPC:
		{
			// payload = [RpcHeader][FlatBuffer]
			if (payloadSize < sizeof(RpcHeader))
				return;

			RpcHeader* rpcHeader = reinterpret_cast<RpcHeader*>(payload);
			BYTE* flatBufferData = payload + sizeof(RpcHeader);
			uint32 flatBufferSize = payloadSize - sizeof(RpcHeader);

			// RpcManager로 디스패치
			m_rpcManager->Dispatch(GetSession(),
				rpcHeader->rpcId,
				rpcHeader->requestId,
				rpcHeader->flags,
				flatBufferData,
				flatBufferSize);
			break;
		}
		case eRpcPacketId::PROTOBUF_RPC:
			// todo
			break;
		case eRpcPacketId::JSON_RPC:
			// todo
			break;
		case eRpcPacketId::BINARY_RPC:
			// todo
			break;
		}
	}

	void UdpSession::HandleAckPacket(uint8 id, BYTE* payload, uint32 payloadSize)
	{
		switch (static_cast<eAckPacketId>(id))
		{
		case eAckPacketId::RELIABILITY_ACK:
		{
			if (payloadSize >= sizeof(AckHeader)) 
			{
				AckHeader* ackData = reinterpret_cast<AckHeader*>(payload);
				m_reliableTransportManager->OnRecvAck(ackData->latestSeq, ackData->bitfield);
				m_netStatTracker->OnRecvAck(ackData->latestSeq);
				m_congestionController->OnRecvAck();

				ProcessQueuedSendBuffer();
			}
			break;
		}
		default:
			break;
		}
	}

	void UdpSession::HandleCustomPacket(uint8 id, BYTE* payload, uint32 payloadSize)
	{
		//todo
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


	//bool UdpSession::CanSend() const
	//{
	//	return m_congestionController->CanSend(m_reliableTransportManager->GetInFlightSize());
	//}

	void UdpSession::ProcessUpdate()
	{
		//if (m_state == eSessionState::DISCONNECTED)
		//	return;

		//if (m_state == eSessionState::HANDSHAKING)
		//{
		//	if (m_handshakeManager)
		//	{
		//		m_handshakeManager->Update();
		//	}
		//	return;
		//}


		//if (m_netStatTracker)
		//{
		//	m_netStatTracker->Update();
		//}

		//if (m_reliableTransportManager)
		//{
		//	m_reliableTransportManager->Update();
		//}
	}


	void UdpSession::SendDirect(const Sptr<SendBuffer>& buf)
	{
		// todo: post to io-thread (in Service::GE)
		
		GetService()->m_udpRouter->RegisterSend(buf, GetRemoteNetAddress());
	}

	//void UdpSession::SendSinglePacket(const Sptr<SendBuffer>& buf)
	//{
	//	PacketAnalysis analysis = PacketBuilder::AnalyzePacket(buf->Buffer(), buf->WriteSize());
	//	if (analysis.isValid && analysis.IsReliable())
	//	{
	//		uint16 seq = analysis.GetSequence();
	//		m_reliableTransportManager->AddPendingPacket(seq, buf, utils::Clock::Instance().GetCurrentTick());
	//	}

	//	if (!m_reliableTransportManager->TryAttachPiggybackAck(buf))
	//	{
	//		m_reliableTransportManager->FailedAttachPiggybackAck();
	//	}

	//	SendDirect(buf);
	//}

	//void UdpSession::SendMultiplePacket(const xvector<Sptr<SendBuffer>>& fragments)
	//{
	//	for (auto& fragment : fragments)
	//	{
	//		SendSinglePacket(fragment);
	//	}
	//}

	void UdpSession::ProcessSend(const Sptr<SendBuffer>& buf)
	{
		//if (!CanSend())
		//{
		//	if (m_sendQueue.size() <= MAX_SENDQUEUE_SIZE)
		//	{
		//		m_sendQueue.push(buf);
		//	}
		//	return;
		//}

		//PacketAnalysis analysis = PacketBuilder::AnalyzePacket(buf->Buffer(), buf->WriteSize());
		//if (!analysis.isValid)
		//	return;

		//if (analysis.payloadSize > MAX_PAYLOAD_SIZE)
		//{
		//	auto fragments = m_fragmentManager->Fragmentize(buf, analysis);
		//	SendMultiplePacket(fragments);
		//}
		//else
		//{
		//	SendSinglePacket(buf);
		//}
	}

	void UdpSession::ProcessQueuedSendBuffer()
	{
		while (!m_sendQueue.empty() && CanSend())
		{
			auto buf = m_sendQueue.front();
			m_sendQueue.pop();
			ProcessSend(buf);
		}
	}



	void UdpSession::ProcessReassembledPayload(const xvector<BYTE>& payload, const PacketAnalysis& first)
	{
		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		const uint8 originalId = first.GetId();
		const ePacketType originalType = first.GetType();

		switch (originalType)
		{
		case ePacketType::SYSTEM: 
		{
			xvector<BYTE> data(payload.begin(), payload.end());
			self->PostCtrl(utils::job::Job([self, id = originalId, data = std::move(data)]() mutable {
					self->HandleSystemPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		case ePacketType::ACK: 
		{
			xvector<BYTE> data(payload.begin(), payload.end());
			self->PostCtrl(utils::job::Job([self, id = originalId, data = std::move(data)]() mutable {
					self->HandleAckPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		case ePacketType::RPC: 
		{
			xvector<BYTE> data(payload.begin(), payload.end());
			self->Post(utils::job::Job([self, id = originalId, data = std::move(data)]() mutable {
					self->HandleRpcPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		case ePacketType::CUSTOM: 
		{
			xvector<BYTE> data(payload.begin(), payload.end());
			self->Post(utils::job::Job([self, id = originalId, data = std::move(data)]() mutable {
					self->HandleCustomPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		}
	}

	void UdpSession::ProcessBufferedPacket(const PacketAnalysis& analysis, BYTE* payload, uint32 payloadSize)
	{
		auto self = static_pointer_cast<UdpSession>(shared_from_this());
		const uint8 id = analysis.GetId();

		switch (analysis.GetType())
		{
		case ePacketType::SYSTEM: 
		{
			xvector<BYTE> data(payload, payload + payloadSize);
			self->PostCtrl(utils::job::Job([self, id, data = std::move(data)]() mutable {
					self->HandleSystemPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		case ePacketType::ACK: 
		{
			xvector<BYTE> data(payload, payload + payloadSize);
			self->PostCtrl(utils::job::Job([self, id, data = std::move(data)]() mutable {
					self->HandleAckPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		case ePacketType::RPC: 
		{
			xvector<BYTE> data(payload, payload + payloadSize);
			self->Post(utils::job::Job([self, id, data = std::move(data)]() mutable {
					self->HandleRpcPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		case ePacketType::CUSTOM: 
		{
			xvector<BYTE> data(payload, payload + payloadSize);
			self->Post(utils::job::Job([self, id, data = std::move(data)]() mutable {
					self->HandleCustomPacket(id, data.data(), static_cast<uint32>(data.size()));
				}));
			break;
		}
		}
	}
}
