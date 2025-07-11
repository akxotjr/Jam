#include "pch.h"
#include "IocpEvent.h"

namespace jam::net
{
	IocpEvent::IocpEvent(eEventType type) : m_eventType(type)
	{
		Init();
	}

	void IocpEvent::Init()
	{
		OVERLAPPED::hEvent = nullptr;
		OVERLAPPED::Internal = 0;
		OVERLAPPED::InternalHigh = 0;
		OVERLAPPED::Offset = 0;
		OVERLAPPED::OffsetHigh = 0;
	}
}
