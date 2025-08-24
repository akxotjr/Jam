#include "pch.h"
#include "FiberScheduler.h"

namespace jam::utils::thrd
{
	void FiberScheduler::AttachToCurrentThread()
	{
		m_main = m_backend.ConvertThreadToMainFiber();
		EnsureFlsKey();
		m_mainCtx.scheduler = this;
		m_mainCtx.fiberId	= 0;
		SetFlsCtx(&m_mainCtx);

		m_resumeCtok = std::make_unique<moodycamel::ConsumerToken>(m_resumeInbox);
		m_spawnCtok = std::make_unique<moodycamel::ConsumerToken>(m_spawnInbox);
	}

	void FiberScheduler::DetachFromThread()
	{
		m_resumeCtok.reset();
		m_spawnCtok.reset();

		SetFlsCtx(nullptr);
		m_backend.RevertMainFiber(m_main);
		m_main = nullptr;
	}

	uint32 FiberScheduler::SpawnFiber(FiberFn fn, const FiberDesc& desc)
	{
		const uint32_t id = m_nextId++;
		auto f = std::make_unique<Fiber>();
		f->id			= id;
		f->entry		= std::move(fn);
		f->state		= eFiberState::READY;
		f->name			= desc.name;
		f->reserve		= desc.stackReserve ? desc.stackReserve : kDefReserve;
		f->commit		= desc.stackCommit ? desc.stackCommit : kDefCommit;

		f->param.self	= this;
		f->param.id		= id;

		f->ctx			= m_backend.CreateFiberSized(f->reserve, f->commit, &f->param, &FiberScheduler::Trampoline);

		m_fibers.emplace(id, std::move(f));
		m_readyQ.push_back(id);

		return id;
	}

	void FiberScheduler::YieldFiber()
	{
		Fiber* f = CurrentFiber();
		f->state = eFiberState::READY;
		m_readyQ.push_back(f->id);
		m_backend.SwitchTo(m_main);
	}

	void FiberScheduler::SleepUntil(uint64 tick)
	{
		Fiber* f = CurrentFiber();
		f->state = eFiberState::WAITING_TIMER;
		f->wakeupTick = tick;
		m_sleepQ.push({ tick, f->id });
		m_backend.SwitchTo(m_main);
	}

	bool FiberScheduler::Suspend(AwaitKey key, uint64 deadline)
	{
		Fiber* f = CurrentFiber();
		f->state = eFiberState::WAITING_EXTERNAL;
		f->awaitKey = key;
		f->resume = eResumeCode::NONE;
		m_waitMap.emplace(key, f->id);

		if (deadline) 
		{
			f->deadline = deadline;
			m_sleepQ.push({ deadline, f->id });
		}
		m_backend.SwitchTo(m_main); // 재개되면 아래로 이어짐
		return (f->resume == eResumeCode::SIGNALED);
	}

	bool FiberScheduler::Resume(AwaitKey key)
	{
		auto it = m_waitMap.find(key);
		if (it == m_waitMap.end()) 
			return false;
		const uint32_t id = it->second;
		m_waitMap.erase(it);

		auto fit = m_fibers.find(id);
		if (fit == m_fibers.end()) 
			return false;

		Fiber* f = fit->second.get();
		if (f->state != eFiberState::WAITING_EXTERNAL) 
			return false;

		f->awaitKey = 0;
		f->resume = eResumeCode::SIGNALED;
		MakeReady(id);
		return true;
	}

	void FiberScheduler::PostResume(AwaitKey key)
	{
		static thread_local std::unique_ptr<moodycamel::ProducerToken> tl_producerToken;
		if (!tl_producerToken)
			tl_producerToken = std::make_unique<moodycamel::ProducerToken>(m_resumeInbox);

		m_resumeInbox.enqueue(*tl_producerToken, ResumeMsg{ key });
	}

	void FiberScheduler::PostSpawn(FiberFn fn, FiberDesc desc)
	{
		static thread_local std::unique_ptr<moodycamel::ProducerToken> tl_producerToken;
		if (!tl_producerToken)
			tl_producerToken = std::make_unique<moodycamel::ProducerToken>(m_spawnInbox);	// resumeInbox 와 spawnInbox 의 구분없이 같은 producer token을 만들어도 되나??

		m_spawnInbox.enqueue(*tl_producerToken, SpawnMsg{ std::move(fn), desc });
	}

	void FiberScheduler::DrainInbox()
	{
		ResumeMsg r;
		while (m_resumeInbox.try_dequeue(*m_resumeCtok, r)) { Resume(r.key); }
		SpawnMsg s;
		while (m_spawnInbox.try_dequeue(*m_spawnCtok, s)) { SpawnFiber(std::move(s.fn), s.desc); }
	}

	void FiberScheduler::Poll(int32 budget, uint64 now)
	{
		// 1) 타이머/타임아웃 기상
		while (!m_sleepQ.empty() && m_sleepQ.top().first <= now) 
		{
			const uint32_t id = m_sleepQ.top().second;
			m_sleepQ.pop();
			auto it = m_fibers.find(id);
			if (it == m_fibers.end()) 
				continue;

			Fiber* f = it->second.get();

			if (f->state == eFiberState::WAITING_TIMER) 
			{
				f->wakeupTick = 0;
				MakeReady(id);
			}
			else if (f->state == eFiberState::WAITING_EXTERNAL) 
			{
				if (f->awaitKey) 
				{
					auto w = m_waitMap.find(f->awaitKey);
					if (w != m_waitMap.end() && w->second == id) 
					{
						m_waitMap.erase(w);        // 타임아웃으로 소유 제거
					}
					f->resume = eResumeCode::TIMEOUT;
					f->awaitKey = 0;
					MakeReady(id);
				}
			}
		}

		// 2) ready 실행 (budget 만큼)
		int steps = 0;
		while (steps < budget && !m_readyQ.empty()) 
		{
			const uint32_t id = m_readyQ.front();
			m_readyQ.pop_front();

			auto it = m_fibers.find(id);
			if (it == m_fibers.end()) 
				continue;

			Fiber* f = it->second.get();
			if (f->state != eFiberState::READY) 
				continue;

			BindFls(f);
			m_currentId = id;

			m_backend.SwitchTo(f->ctx); // 파이버 한 스텝 실행 → Yield/Suspend/Terminate 시 메인 복귀

			m_currentId = 0;
			++steps;

			if (f->state == eFiberState::TERMINATED) 
			{
				m_backend.DestroyFiber(f->ctx);
				m_fibers.erase(id);
			}
		}

		// 3) 인박스 한 번 더 비우기(레이턴시↓)
		DrainInbox();
	}

	uint32 FiberScheduler::Current() const
	{
		if (auto* c = GetFlsCtx()) 
			return c->fiberId;
		return 0;
	}

	void FiberScheduler::Trampoline(void* p)
	{
		auto* prm = reinterpret_cast<TrampolineParam*>(p);
		auto* self = prm->self;
		const uint32_t id = prm->id;

		auto it = self->m_fibers.find(id);
		if (it == self->m_fibers.end())
		{
			self->m_backend.SwitchTo(self->m_main);
			return;
		}
		Fiber* f = it->second.get();

		self->BindFls(f);

		try {
			f->entry(); // 사용자 코드
		}
		catch (const std::exception& e) {
			self->OnFiberException(id, e.what());
		}
		catch (...) {
			self->OnFiberException(id, "unknown exception");
		}

		f->state = eFiberState::TERMINATED;
		self->m_backend.SwitchTo(self->m_main);
	}

	void FiberScheduler::MakeReady(uint32_t id)
	{
		auto it = m_fibers.find(id);
		if (it == m_fibers.end()) 
			return;
		Fiber* f = it->second.get();
		f->state = eFiberState::READY;
		m_readyQ.push_back(id);
	}

	void FiberScheduler::OnFiberException(uint32_t id, const char* what)
	{
		// TODO: 프로젝트 로거로 치환
		(void)id; (void)what;
		// 예: Logger::Error("[Fiber %u] exception: %s", id, what);
	}

	FiberScheduler::Fiber* FiberScheduler::CurrentFiber()
	{
		auto it = m_fibers.find(Current());
		if (it == m_fibers.end()) 
			throw std::runtime_error("No current fiber");
		return it->second.get();
	}

	void FiberScheduler::BindFls(Fiber* f)
	{
		f->fls.scheduler = this;
		f->fls.fiberId = f->id;
		SetFlsCtx(&f->fls);
	}
}
