#include "pch.h"
#include "jamnet/core/net/IocpCore.h"
#include "jamnet/core/net/IocpEvent.h"

namespace jam::net
{
	IocpCore::IocpCore()
	{
		m_iocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
		JAMNET_ASSERT(m_iocpHandle != INVALID_HANDLE_VALUE)
	}

	IocpCore::~IocpCore()
	{
		::CloseHandle(m_iocpHandle);
	}

	bool IocpCore::Register(const shared_ptr<IocpObject>& obj)
	{
		if (!obj) return false;
		HANDLE h = obj->GetHandle();
		if (h == INVALID_HANDLE_VALUE)
			return false;

		if (::CreateIoCompletionPort(h, m_iocpHandle, 0, 0) == NULL)
		{
			DWORD ec = ::GetLastError();
			cout << "[IOCP] Register failed ec=" << ec << "\n";
			return false;
		}
		return true;
	}

	bool IocpCore::Dispatch(uint32 timeoutMs)
	{
		DWORD numOfBytes = 0;
		ULONG_PTR key = 0;
		IocpEvent* iocpEvent = nullptr;

		if (::GetQueuedCompletionStatus(m_iocpHandle, OUT &numOfBytes, OUT &key, OUT reinterpret_cast<LPOVERLAPPED*>(&iocpEvent), timeoutMs))
		{
			if (iocpEvent == nullptr) return false;

			shared_ptr<IocpObject> iocpObject = iocpEvent->m_owner;
			iocpObject->Dispatch(iocpEvent, numOfBytes);
		}
		else
		{
			switch (const int32 errorCode = ::WSAGetLastError())
			{
			case WAIT_TIMEOUT:
				return false;
			default:
				shared_ptr<IocpObject> iocpObject = iocpEvent->m_owner;
				iocpObject->Dispatch(iocpEvent, numOfBytes);
				break;
			}
		}

		return true;
	}
}
