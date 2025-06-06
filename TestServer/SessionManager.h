#pragma once

class Service;
class SendBuffer;

struct SessionBundle
{
	Sptr<GameTcpSession> tcpSession = nullptr;
	Sptr<GameUdpSession> udpSession = nullptr;

	bool IsPaired() const
	{
		return tcpSession != nullptr && udpSession != nullptr;
	}
};

class SessionManager
{
	DECLARE_SINGLETON(SessionManager)

public:
	void							Add(Sptr<Session> session);
	void							Remove(Sptr<Session> session);

	void							Multicast(EProtocolType type, Sptr<jam::net::SendBuffer> sendBuffer, bool reliable = false);

	Sptr<Session>					GetSessionByUserId(EProtocolType type, uint32 userId);

private:
	USE_LOCK

	xumap<uint32, SessionBundle>	_sessionMap;
};

