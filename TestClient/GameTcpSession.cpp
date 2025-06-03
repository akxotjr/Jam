#include "pch.h"
#include "GameTcpSession.h"

GameTcpSession::GameTcpSession()
{
}

GameTcpSession::~GameTcpSession()
{
}

void GameTcpSession::OnConnected()
{
	cout << "[TCP] OnConnected\n";
}

void GameTcpSession::OnDisconnected()
{
	cout << "[TCP] OnDisconnected\n";
}

void GameTcpSession::OnSend(int32 len)
{
	cout << "[TCP] OnSend : " << len << "\n";
}

void GameTcpSession::OnRecv(BYTE* buffer, int32 len)
{
	cout << "[TCP] OnRecv : " << len << "\n";
}
