#pragma once

class GameTcpSession : public TcpSession
{
public:
	GameTcpSession();
	virtual ~GameTcpSession() override;

	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual void	OnSend(int32 len) override;
	virtual void	OnRecv(BYTE* buffer, int32 len) override;
};

