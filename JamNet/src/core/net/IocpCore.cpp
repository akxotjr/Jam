#include "pch.h"
#include "jamnet/core/net/IocpCore.h"
#include "jamnet/core/net/IocpEvent.h"

namespace jam::net
{

	IocpCore::IocpCore()
	{
		m_iocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
		JAM_ASSERT(m_iocpHandle != nullptr);
	}

	IocpCore::~IocpCore()
	{
		if (m_iocpHandle)
			::CloseHandle(m_iocpHandle);
	}

	bool IocpCore::Register(const std::shared_ptr<IocpObject>& obj)
	{
		if (!obj) return false;
		HANDLE h = obj->GetHandle();
		if (h == INVALID_HANDLE_VALUE)
			return false;

		if (::CreateIoCompletionPort(h, m_iocpHandle, 0, 0) == nullptr)
		{
			DWORD ec = ::GetLastError();
			std::cout << "[IOCP] Register failed ec=" << ec << "\n";
			return false;
		}
		return true;
	}

	bool IocpCore::Dispatch(uint32 timeout_ms)
	{
		DWORD	   bytse = 0;
		ULONG_PTR  key	 = 0;
		IocpEvent* event = nullptr;

		if (::GetQueuedCompletionStatus(m_iocpHandle, OUT &bytse, OUT &key, OUT reinterpret_cast<LPOVERLAPPED*>(&event), timeout_ms))
		{
			if (event == nullptr) return false;

			std::shared_ptr<IocpObject> iocpObject = event->m_owner;
			iocpObject->Dispatch(event, static_cast<int32>(bytse));
		}
		else
		{
			switch (const int32 errorCode = ::WSAGetLastError())
			{
			case WAIT_TIMEOUT:
				return false;
			default:
				std::shared_ptr<IocpObject> iocpObject = event->m_owner;
				iocpObject->Dispatch(event, static_cast<int32>(bytse));
				break;
			}
		}

		return true;
	}

	bool IocpCore::Post(IocpEvent* event, int32 bytes)
	{
		if (!event)
			return false;

		return ::PostQueuedCompletionStatus(m_iocpHandle, static_cast<DWORD>(bytes), 0, event) != FALSE;
	}

	void IocpCore::Wake(uint32 count)
	{
		for (uint32 i = 0; i < count; ++i)
			::PostQueuedCompletionStatus(m_iocpHandle, 0, 0, nullptr);
	}
}
