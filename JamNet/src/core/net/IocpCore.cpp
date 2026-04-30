#include "pch.h"
#include "jamnet/core/net/IocpCore.h"
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/WinErrorHandling.h"

namespace jam::net
{
	bool IocpObject::TryAddPendingDispatch()
	{
		if (m_closing.load(std::memory_order_acquire))
			return false;

		m_pendingDispatchCount.fetch_add(1, std::memory_order_acq_rel);
		if (m_closing.load(std::memory_order_acquire))
		{
			m_pendingDispatchCount.fetch_sub(1, std::memory_order_acq_rel);
			return false;
		}

		return true;
	}




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

	bool IocpCore::Register(IocpObject* obj)
	{
		if (!obj) return false;
		HANDLE h = obj->GetHandle();
		if (h == INVALID_HANDLE_VALUE)
			return false;

		if (::CreateIoCompletionPort(h, m_iocpHandle, reinterpret_cast<ULONG_PTR>(obj), 0) == nullptr)
		{
			win_error::LogLastWinError("[IOCP] Register");
			return false;
		}
		return true;
	}

	bool IocpCore::Dispatch(uint32 timeout_ms)
	{
		OVERLAPPED_ENTRY entries[128];
		ULONG numEntries = 0;
		const BOOL ok = ::GetQueuedCompletionStatusEx(m_iocpHandle, entries, 128, &numEntries, timeout_ms, FALSE);
		if (!ok)
		{
			const DWORD error = win_error::GetLastWinError();
			if (error == WAIT_TIMEOUT && numEntries == 0)
				return false;
		}

		for (ULONG i = 0; i < numEntries; ++i)
		{
			auto& entry = entries[i];

			IocpObject* iocpObject = reinterpret_cast<IocpObject*>(entry.lpCompletionKey);
			IocpEvent*  iocpEvent  = static_cast<IocpEvent*>(entry.lpOverlapped);
			int32		bytes	   = static_cast<int32>(entry.dwNumberOfBytesTransferred);

			if (iocpObject == nullptr || iocpEvent == nullptr)
				continue;	// wake / empty packet

			iocpObject->Dispatch(iocpEvent, bytes);
			iocpObject->ReleasePendingDispatch();
			if (iocpObject->IsClosing() && iocpObject->GetPendingDispatchCount() == 0)
				iocpObject->OnPendingDispatchDrained();
		}

		return ok != FALSE || numEntries != 0;
	}

	bool IocpCore::Post(IocpObject* obj, IocpEvent* event, int32 bytes)
	{
		if (!obj || !event)
			return false;

		if (!obj->TryAddPendingDispatch())
			return false;

		if (::PostQueuedCompletionStatus(m_iocpHandle, static_cast<DWORD>(bytes), reinterpret_cast<ULONG_PTR>(obj), event) == FALSE)
		{
			obj->ReleasePendingDispatch();
			return false;
		}

		return true;
	}

	void IocpCore::Wake(uint32 count)
	{
		for (uint32 i = 0; i < count; ++i)
			::PostQueuedCompletionStatus(m_iocpHandle, 0, 0, nullptr);
	}
}
