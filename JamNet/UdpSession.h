#pragma once
#include "Session.h"
#include "RecvBuffer.h"



namespace jam::net
{
	class CongestionController;
	class ReliableTransportManager;
	class FragmentHandler;
	class NetStatTracker;
	class HandshakeManager;
	

	/*--------------------------
		 ReliableUdpSession
	---------------------------*/
	
	//------------------------------------------------------------------------//

	struct RudpHeader
	{
		uint16 sequence = 0;
	};

	//------------------------------------------------------------------------//



	//------------------------------------------------------------------------//

#pragma pack(push, 1)
	struct AckHeader
	{
		uint16 latestSeq = 0;
		uint32 bitfield;
	};
#pragma pack(pop)
	
	//------------------------------------------------------------------------//


	struct PendingPacket
	{
		Sptr<SendBuffer>	buffer;
		uint16				sequence;
		uint64				timestamp;
		uint32				retryCount = 0;
	};

	struct PING
	{
		uint64 clientSendTick;
	};

	struct PONG
	{
		uint64 clientSendTick;
		uint64 serverSendTick;
	};

	constexpr int32		MAX_HANDSHAKE_RETRIES = 5;
	constexpr double	HANDSHAKE_RETRY_INTERVAL = 0.5; 

	class UdpSession : public Session
	{
		enum { BUFFER_SIZE = 0x10000 }; // 64KB

		friend class UdpReceiver;
		friend class IocpCore;
		friend class Service;

		friend class CongestionController;
		friend class NetStatTracker;
		friend class HandshakeManager;

	public:
		UdpSession();
		virtual ~UdpSession() override = default;

		virtual bool							Connect() override;
		virtual void							Disconnect(const WCHAR* cause) override;
		virtual void							Send(const Sptr<SendBuffer>& buf) override;

	private:
		/* Iocp Object impl */
		virtual HANDLE							GetHandle() override;
		virtual void							Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

	public:

		void									ProcessDisconnect();
		void									ProcessSend(int32 numOfBytes);
		void									ProcessRecv(int32 numOfBytes, RecvBuffer& recvBuffer);


		int32									ParsePacket(BYTE* buffer, int32 len);
		void									HandleSystemPacket(BYTE* buffer, uint32 size, PacketBuilder& pb);
		void									HandleRpcPacket(BYTE* buffer, uint32 size, PacketBuilder& pb);
		void									HandleAckPacket(BYTE* buffer, uint32 size, PacketBuilder& pb);
		void									HandleCustomPacket(BYTE* data, uint32 len);

		void									UpdateRetry();

		void									HandleError(int32 errorCode);


	private:
		void									SendDirect(const Sptr<SendBuffer>& buf);

		CongestionController*					GetCongestionController() { return m_congestionController.get(); }
		NetStatTracker*							GetNetStatTracker() { return m_netStatTracker.get(); }


	private:
		USE_LOCK

		RecvBuffer								m_recvBuffer;

		Uptr<HandshakeManager>					m_handshakeManager = nullptr;
		Uptr<NetStatTracker>					m_netStatTracker = nullptr;
		Uptr<CongestionController>				m_congestionController = nullptr;
		Uptr<FragmentHandler>					m_fragmentHandler = nullptr;
		Uptr<ReliableTransportManager> 			m_reliableTransportManager = nullptr;
	};
}

