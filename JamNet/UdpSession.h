#pragma once
#include "Session.h"
#include "RecvBuffer.h"



namespace jam::net
{
	struct PacketAnalysis;
	class CongestionController;
	class ReliableTransportManager;
	class FragmentManager;
	class NetStatManager;
	class HandshakeManager;
	class ChannelManager;
	

	/*--------------------------
		 ReliableUdpSession
	---------------------------*/

#pragma pack(push, 1)
	struct AckHeader
	{
		uint16 latestSeq = 0;
		uint32 bitfield;
	};
#pragma pack(pop)


	struct PING
	{
		uint64 clientSendTick;
	};

	struct PONG
	{
		uint64 clientSendTick;
		uint64 serverSendTick;
	};

	//constexpr int32		MAX_HANDSHAKE_RETRIES = 5;
	//constexpr double	HANDSHAKE_RETRY_INTERVAL = 0.5;







	class UdpSession : public Session
	{
		enum { BUFFER_SIZE = 0x10000 }; // 64KB

		friend class IocpCore;
		friend class Service;

		friend class CongestionController;
		friend class NetStatManager;
		friend class HandshakeManager;
		friend class ReliableTransportManager;
		friend class ChannelManager;

	public:
		UdpSession();
		virtual ~UdpSession() override = default;

		virtual bool							Connect() override;
		virtual void							Disconnect(const WCHAR* cause) override;
		virtual void							Send(const Sptr<SendBuffer>& buf) override;

		virtual void							Update() override;

	private:
		void									OnLinkEstablished();
		void									OnLinkTerminated();

	private:
		/* Iocp Object impl */ 
		virtual HANDLE							GetHandle() override { return HANDLE(); };
		virtual void							Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override {};

	public:
		void									ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer);


		uint32									ParsePacket(BYTE* buf, uint32 size);
		void									HandleSystemPacket(uint8 id, BYTE* payload, uint32 payloadSize);
		void									HandleRpcPacket(uint8 id, BYTE* payload, uint32 payloadSize);
		void									HandleAckPacket(uint8 id, BYTE* payload, uint32 payloadSize);
		void									HandleCustomPacket(uint8 id, BYTE* payload, uint32 payloadSize);

		void									HandleError(int32 errorCode);


	private:
		bool									CanSend() const;

		void									ProcessUpdate();


		void									SendDirect(const Sptr<SendBuffer>& buf);


		void									SendSinglePacket(const Sptr<SendBuffer>& buf);
		void									SendMultiplePacket(const xvector<Sptr<SendBuffer>>& fragments);

		void									ProcessSend(const Sptr<SendBuffer>& buf);
		void									ProcessQueuedSendBuffer();

		CongestionController*					GetCongestionController() { return m_congestionController.get(); }
		NetStatManager*							GetNetStatManager() { return m_netStatTracker.get(); }
		ReliableTransportManager*				GetReliableTransportManager() { return m_reliableTransportManager.get(); }


		void									ProcessReassembledPayload(const xvector<BYTE>& payload, const PacketAnalysis& firstFragmentAnalysis);

		void									ProcessBufferedPacket(const PacketAnalysis& analysis, BYTE* payload, uint32 payloadSize);

	private:
		USE_LOCK

		RecvBuffer								m_recvBuffer;

		xqueue<Sptr<SendBuffer>>				m_sendQueue;
		static constexpr uint32 MAX_SENDQUEUE_SIZE = 100;

		Uptr<HandshakeManager>					m_handshakeManager = nullptr;
		Uptr<NetStatManager>					m_netStatTracker = nullptr;
		Uptr<CongestionController>				m_congestionController = nullptr;
		Uptr<FragmentManager>					m_fragmentManager = nullptr;
		Uptr<ReliableTransportManager> 			m_reliableTransportManager = nullptr;
		Uptr<ChannelManager>					m_channelManager = nullptr;
	};

}

