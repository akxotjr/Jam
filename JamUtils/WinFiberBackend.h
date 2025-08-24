#pragma once
#include "FiberCommon.h"

namespace jam::utils::thrd
{
	struct FlsFiberCtx
	{
		void*	scheduler = nullptr;
		uint32	fiberId = 0;
	};

	DWORD			EnsureFlsKey();
	FlsFiberCtx*	GetFlsCtx();
	void			SetFlsCtx(FlsFiberCtx* ctx);

	struct FiberBackendConfig
	{
		uint64	stackReverse = 512 * 1024;
		uint64	stackCommit = 128 * 1024;
	};



	class WinFiberBackend
	{
	public:
		explicit WinFiberBackend(FiberBackendConfig config = {}) : m_config(config) {}

		void*				ConvertThreadToMainFiber();
		void				RevertMainFiber(void* mainFiber);
		void*				CreateFiber(void* param, void (WINAPI* proc)(void*));
		void*				CreateFiberSized(uint64 reserve, uint64 commit, void* param, void (WINAPI* proc)(void*));
		void				SwitchTo(void* fiber);
		void				DestroyFiber(void* fiber);


	private:
		FiberBackendConfig	m_config;
		bool				m_attached = false;
	};
}



