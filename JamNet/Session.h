#pragma once
#include "IocpCore.h"

namespace jam::net
{
	class Service;
	class NetAddress;

	class Session : public IocpObject
	{
	public:
		Session() = default;
		virtual ~Session() = default;

		virtual bool							Connect() = 0;
		virtual void							Disconnect(const WCHAR* cause) = 0;
		virtual void							Send(Sptr<SendBuffer> sendBuffer) = 0;

		virtual bool							IsTcp() const = 0;
		virtual bool							IsUdp() const = 0;

		Sptr<Service>							GetService() { return m_service.lock(); }
		void									SetService(Sptr<Service> service) { m_service = service; }

		NetAddress&								GetRemoteNetAddress() { return m_remoteAddress; }
		void									SetRemoteNetAddress(NetAddress address) { m_remoteAddress = address; }
		SOCKET									GetSocket() { return m_socket; }
		bool									IsConnected() { return m_connected; }
		Sptr<Session>							GetSessionRef() { return static_pointer_cast<Session>(shared_from_this()); }
		uint32									GetId() { return m_id; }
		void									SetId(uint32 id) { m_id = id; };

	protected:
		virtual void							OnConnected() = 0;
		virtual void							OnDisconnected() = 0;
		virtual void							OnSend(int32 len) = 0;
		virtual void							OnRecv(BYTE* buffer, int32 len) = 0;


	protected:
		SOCKET									m_socket = INVALID_SOCKET;
		Atomic<bool>							m_connected = false;

	private:
		Wptr<Service>							m_service;
		NetAddress								m_remoteAddress = {};
		uint32									m_id = 0;
	};
}
