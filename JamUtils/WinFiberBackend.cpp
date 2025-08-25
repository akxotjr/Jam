#include "pch.h"
#include "WinFiberBackend.h"

#include <algorithm>

namespace jam::utils::thrd
{
	static DWORD g_flsKey = FLS_OUT_OF_INDEXES;

	DWORD EnsureFlsKey()
	{
		if (g_flsKey == FLS_OUT_OF_INDEXES)
		{
			g_flsKey = ::FlsAlloc(nullptr);
			if (g_flsKey == FLS_OUT_OF_INDEXES)
				throw std::runtime_error("FlsAlloc failed");
		}
		return g_flsKey;
	}

	FlsFiberCtx* GetFlsCtx()
	{
		if (g_flsKey == FLS_OUT_OF_INDEXES)
			return nullptr;
		return static_cast<FlsFiberCtx*>(::FlsGetValue(g_flsKey));
	}

	void SetFlsCtx(FlsFiberCtx* ctx)
	{
		::FlsSetValue(EnsureFlsKey(), ctx);
	}




	void* WinFiberBackend::ConvertThreadToMainFiber()
	{
		if (m_attached) 
			return GetCurrentFiber();

		LPVOID mf = ::ConvertThreadToFiberEx(nullptr, 0);

		if (!mf) 
			throw std::runtime_error("ConvertThreadToFiberEx failed");

		m_attached = true;
		return mf;
	}

	void WinFiberBackend::RevertMainFiber(void* mainFiber)
	{
		if (m_attached)
		{
			::ConvertFiberToThread();
			m_attached = false;
		}
	}

	void* WinFiberBackend::CreateFiber(void* param, void (WINAPI *proc)(void*))
	{
		return CreateFiberSized(m_config.stackReserve, m_config.stackCommit, param, proc);
	}

	void* WinFiberBackend::CreateFiberSized(uint64 reserve, uint64 commit, void* param, void (WINAPI *proc)(void*))
	{
		commit = min(commit, reserve);

		LPVOID fiber = ::CreateFiberEx(commit, reserve, 0, proc, param);
		if (!fiber)
			throw std::runtime_error("CreateFiberEx failed");

		return fiber;
	}

	void WinFiberBackend::SwitchTo(void* fiber)
	{
		::SwitchToFiber(fiber);
	}

	void WinFiberBackend::DestroyFiber(void* fiber)
	{
		::DeleteFiber(fiber);
	}

	bool WinFiberBackend::ProbeCurrentFiberStack(uint64& used, uint64& total)
	{
		using Fn = VOID(WINAPI*)(PULONG_PTR, PULONG_PTR);
		static auto p = reinterpret_cast<Fn>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "GetCurrentThreadStackLimits"));

		if (!p)
			return false;

		ULONG_PTR lo = 0, hi = 0;
		p(&lo, &hi);
		uint8 local;
		ULONG_PTR sp = reinterpret_cast<ULONG_PTR>(&local);
		if (sp < lo || sp > hi)
			return false;

		total = static_cast<uint64>(hi - lo);
		used = static_cast<uint64>(hi - sp);

		return true;
	}
}
