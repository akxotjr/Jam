#pragma once
#include "IocpCore.h"

namespace jam::net
{
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

		Sptr<Service>							GetService() { return _service.lock(); }
		void									SetService(Sptr<Service> service) { _service = service; }

		NetAddress& GetRemoteNetAddress() { return _remoteAddress; }
		void									SetRemoteNetAddress(NetAddress address) { _remoteAddress = address; }
		SOCKET									GetSocket() { return _socket; }	//todo
		bool									IsConnected() { return _connected; }
		Sptr<Session>							GetSessionRef() { return static_pointer_cast<Session>(shared_from_this()); }
		uint32									GetId() { return _id; }
		void									SetId(uint32 id) { _id = id; }

	protected:
		virtual void							OnConnected() {}
		virtual void							OnDisconnected() {}
		virtual void							OnRecv(BYTE* buffer, int32 len) {}
		virtual void							OnSend(int32 len) {}


	protected:
		SOCKET									_socket = INVALID_SOCKET;
		Atomic<bool>							_connected = false;

	private:
		weak_ptr<Service>						_service;
		NetAddress								_remoteAddress = {};
		uint32									_id = 0;
	};
}
