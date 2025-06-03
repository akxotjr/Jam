#pragma once

// client
class GameUdpSession : public UdpSession
{
public:
	GameUdpSession();
	virtual ~GameUdpSession() override;

	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual void	OnSend(int32 len) override;
	virtual void	OnRecv(BYTE* buffer, int32 len) override;
};

