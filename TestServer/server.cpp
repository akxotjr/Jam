#include "pch.h"

#include "DeadLockProfiler.h"
#include "GameTcpSession.h"
#include "GameUdpSession.h"
#include "Logger.h"
#include "TimeManager.h"

int main()
{
	MemoryManager::Instance().Init();
	TimeManager::Instance().Init();
	Logger::Instance().Init();
	SocketUtils::Init();

	TransportConfig config = {
		.localTcpAddress = NetAddress(L"127.0.0.1", 7777),
		.localUdpAddress = NetAddress(L"127.0.0.1", 8888)
	};

	Sptr<ServerService> service = MakeShared<ServerService>(config, 50, 50);

	service->SetSessionFactory<GameTcpSession, GameUdpSession>();
	ASSERT_CRASH(service->Start());

	LOG_INFO("Server Service start");

	while (true)
	{
		service->GetIocpCore()->Dispatch(10);

		this_thread::sleep_for(1ms);
	}
}
