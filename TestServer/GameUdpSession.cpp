#include "pch.h"
#include "GameUdpSession.h"

GameUdpSession::GameUdpSession()
{
}

GameUdpSession::~GameUdpSession()
{
}

void GameUdpSession::OnConnected()
{
	cout << "[UDP] OnConnected\n";
}

void GameUdpSession::OnDisconnected()
{
	cout << "[UDP] OnDisconnected\n";
}

void GameUdpSession::OnSend(int32 len)
{
	cout << "[UDP] OnSend : " << len << "\n";
}

void GameUdpSession::OnRecv(BYTE* buffer, int32 len)
{
	cout << "[UDP] OnRecv : " << len << "\n";
}