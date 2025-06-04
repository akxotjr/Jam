#include "pch.h"

#include "GameTcpSession.h"
#include "GameUdpSession.h"
#include "Logger.h"
#include "TimeManager.h"

int main()
{
	this_thread::sleep_for(10ms);

	MemoryManager::Instance().Init();
	TimeManager::Instance().Init();
	Logger::Instance().Init();
	SocketUtils::Init();

	TransportConfig config = {
		.remoteTcpAddress = NetAddress(L"127.0.0.1", 7777),
		.remoteUdpAddress = NetAddress(L"127.0.0.1", 8888)
	};

	Sptr<ClientService> service = MakeShared<ClientService>(config, 50, 50);

	service->SetSessionFactory<GameTcpSession, GameUdpSession>();
	ASSERT_CRASH(service->Start());

	LOG_INFO("Client Service start");

	Sptr<Session> session = service->CreateSession(EProtocolType::UDP);
	if (session == nullptr)
		return false;

	service->AddHandshakingUdpSession(static_pointer_cast<UdpSession>(session));
	ASSERT_CRASH(session->Connect())

	while (true)
	{
		service->GetIocpCore()->Dispatch(10);

		this_thread::sleep_for(1ms);
	}

	return 0;
}
