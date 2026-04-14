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

		if (::CreateIoCompletionPort(h, m_iocpHandle, 0, 0) == NULL)
		{
			DWORD ec = ::GetLastError();
			std::cout << "[IOCP] Register failed ec=" << ec << "\n";
			return false;
		}
		return true;
	}

	bool IocpCore::Dispatch(uint32 timeoutMs)
	{
		DWORD	   numOfBytes = 0;
		ULONG_PTR  key		  = 0;
		IocpEvent* iocpEvent  = nullptr;

		if (::GetQueuedCompletionStatus(m_iocpHandle, OUT &numOfBytes, OUT &key, OUT reinterpret_cast<LPOVERLAPPED*>(&iocpEvent), timeoutMs))
		{
			if (iocpEvent == nullptr) return false;

			std::shared_ptr<IocpObject> iocpObject = iocpEvent->m_owner;
			iocpObject->Dispatch(iocpEvent, static_cast<int32>(numOfBytes));
		}
		else
		{
			switch (const int32 errorCode = ::WSAGetLastError())
			{
			case WAIT_TIMEOUT:
				return false;
			default:
				std::shared_ptr<IocpObject> iocpObject = iocpEvent->m_owner;
				iocpObject->Dispatch(iocpEvent, static_cast<int32>(numOfBytes));
				break;
			}
		}

		return true;
	}

	void IocpCore::Wake(uint32 count)
	{
		for (uint32 i = 0; i < count; ++i)
			::PostQueuedCompletionStatus(m_iocpHandle, 0, 0, nullptr);
	}
}
