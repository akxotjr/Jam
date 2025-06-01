#include "pch.h"
#include "IocpCore.h"
#include "IocpEvent.h"

namespace jam::net
{
	IocpCore::IocpCore()
	{
		m_iocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
		ASSERT_CRASH(m_iocpHandle != INVALID_HANDLE_VALUE)
	}

	IocpCore::~IocpCore()
	{
		::CloseHandle(m_iocpHandle);
	}

	bool IocpCore::Register(const Sptr<IocpObject>& iocpObject)
	{
		return ::CreateIoCompletionPort(iocpObject->GetHandle(), m_iocpHandle, 0, 0);
	}

	bool IocpCore::Dispatch(uint32 timeoutMs)
	{
		DWORD numOfBytes = 0;
		ULONG_PTR key = 0;
		IocpEvent* iocpEvent = nullptr;

		if (::GetQueuedCompletionStatus(m_iocpHandle, OUT &numOfBytes, OUT &key, OUT reinterpret_cast<LPOVERLAPPED*>(&iocpEvent), timeoutMs))
		{
			if (iocpEvent == nullptr) return false;

			Sptr<IocpObject> iocpObject = iocpEvent->m_owner;
			iocpObject->Dispatch(iocpEvent, numOfBytes);
		}
		else
		{
			const int32 errorCode = ::WSAGetLastError();
			switch (errorCode)
			{
			case WAIT_TIMEOUT:
				return false;
			default:
				Sptr<IocpObject> iocpObject = iocpEvent->m_owner;
				iocpObject->Dispatch(iocpEvent, numOfBytes);
				break;
			}
		}

		return true;
	}
}
