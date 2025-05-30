#include "pch.h"
#include "IocpCore.h"

namespace jam::net
{
	IocpCore::IocpCore()
	{
		_iocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
		ASSERT_CRASH(_iocpHandle != INVALID_HANDLE_VALUE)
	}

	IocpCore::~IocpCore()
	{
		::CloseHandle(_iocpHandle);
	}

	bool IocpCore::Register(IocpObjectRef iocpObject)
	{
		return ::CreateIoCompletionPort(iocpObject->GetHandle(), _iocpHandle, 0, 0);
	}

	bool IocpCore::Dispatch(uint32 timeoutMs)
	{
		DWORD numOfBytes = 0;
		ULONG_PTR key = 0;
		IocpEvent* iocpEvent = nullptr;

		if (::GetQueuedCompletionStatus(_iocpHandle, OUT &numOfBytes, OUT &key, OUT reinterpret_cast<LPOVERLAPPED*>(&iocpEvent), timeoutMs))
		{
			if (iocpEvent == nullptr) return false;

			IocpObjectRef iocpObject = iocpEvent->owner;
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
				IocpObjectRef iocpObject = iocpEvent->owner;
				iocpObject->Dispatch(iocpEvent, numOfBytes);
				break;
			}
		}

		return true;
	}
}
