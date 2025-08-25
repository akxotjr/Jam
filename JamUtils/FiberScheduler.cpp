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
		m_cancelKeyCtok = std::make_unique<moodycamel::ConsumerToken>(m_cancelKeyInbox);
		m_cancelIdCtok = std::make_unique<moodycamel::ConsumerToken>(m_cancelIdInbox);
	}

	void FiberScheduler::DetachFromThread()
	{
		m_resumeCtok.reset();
		m_spawnCtok.reset();
		m_cancelKeyCtok.reset();
		m_cancelIdCtok.reset();

		SetFlsCtx(nullptr);
		m_backend.RevertMainFiber(m_main);
		m_main = nullptr;
	}

	uint32 FiberScheduler::SpawnFiber(FiberFn fn, const FiberDesc& desc)
	{
		const uint32 id = m_nextId++;

		Fiber* f = FiberPool::Pop();
		*f = Fiber{};
		
		f->id			= id;
		f->name			= desc.name;
		f->reserve		= desc.stackReserve ? desc.stackReserve : kDefReserve;
		f->commit		= desc.stackCommit ? desc.stackCommit : kDefCommit;
		f->state		= eFiberState::READY;
		f->priority		= desc.priority;
		f->cancel		= desc.cancelToken;
		f->entry		= std::move(fn);
		f->param.self	= this;
		f->param.id		= id;

		f->ctx			= m_backend.CreateFiberSized(f->reserve, f->commit, &f->param, &FiberScheduler::Trampoline);


		m_fibers.emplace(id, f);
		MakeReady(id);

		return id;
	}

	void FiberScheduler::YieldFiber()
	{
		Fiber* f = CurrentFiber();
		f->state = eFiberState::READY;
		MakeReady(f->id);
		m_backend.SwitchTo(m_main);
	}

	void FiberScheduler::SleepUntil(uint64 tick)
	{
		Fiber* f = CurrentFiber();
		f->state = eFiberState::WAITING_TIMER;
		f->wakeupTick = tick;
		m_sleepPQ.push({ tick, f->id });
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
			m_sleepPQ.push({ deadline, f->id });
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

		Fiber* f = fit->second;
		if (f->state != eFiberState::WAITING_EXTERNAL) 
			return false;

		f->awaitKey = 0;
		f->resume = eResumeCode::SIGNALED;
		MakeReady(id);
		return true;
	}

	bool FiberScheduler::CancelByKey(AwaitKey key, eCancelCode code)
	{
		auto it = m_waitMap.find(key);
		if (it == m_waitMap.end()) return false;

		auto fit = m_fibers.find(it->second);
		if (fit == m_fibers.end()) return false;

		Fiber* f = fit->second;
		Wake(f, eResumeCode::CANCELLED, code);
		return true;
	}

	bool FiberScheduler::CancelById(uint32 id, eCancelCode code)
	{
		auto fit = m_fibers.find(id);
		if (fit == m_fibers.end()) return false;

		Fiber* f = fit->second;
		Wake(f, eResumeCode::CANCELLED, code);
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

	void FiberScheduler::PostCancelByKey(AwaitKey key, eCancelCode code)
	{
		static thread_local std::unique_ptr<moodycamel::ProducerToken> tl_producerToken;
		if (!tl_producerToken)
			tl_producerToken = std::make_unique<moodycamel::ProducerToken>(m_cancelKeyInbox);

		m_cancelKeyInbox.enqueue(CancelKeyMsg{ key, code });
	}

	void FiberScheduler::PostCancelById(uint32 id, eCancelCode code)
	{
		static thread_local std::unique_ptr<moodycamel::ProducerToken> tl_producerToken;
		if (!tl_producerToken)
			tl_producerToken = std::make_unique<moodycamel::ProducerToken>(m_cancelIdInbox);

		m_cancelIdInbox.enqueue(CancelIdMsg{ id, code });
	}

	void FiberScheduler::DrainInbox()
	{
		ResumeMsg r;
		while (m_resumeInbox.try_dequeue(*m_resumeCtok, r)) { Resume(r.key); }
		SpawnMsg s;
		while (m_spawnInbox.try_dequeue(*m_spawnCtok, s)) { SpawnFiber(std::move(s.fn), s.desc); }
		CancelKeyMsg ck;
		while (m_cancelKeyInbox.try_dequeue(*m_cancelKeyCtok, ck)) { CancelByKey(ck.key, ck.code); }
		CancelIdMsg ci;
		while (m_cancelIdInbox.try_dequeue(*m_cancelIdCtok, ci)) { CancelById(ci.id, ci.code); }
	}

	void FiberScheduler::Poll(int32 budget, uint64 now)
	{
		const uint64 t0 = NowNs();

		// 1) 타이머/타임아웃 기상
		while (!m_sleepPQ.empty() && m_sleepPQ.top().wakeupTick <= now)
		{
			const uint32 id = m_sleepPQ.top().fiberId;
			m_sleepPQ.pop();
			auto it = m_fibers.find(id);
			if (it == m_fibers.end()) 
				continue;

			Fiber* f = it->second;

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

					if (f->cancel)
						f->cancel->RequestCancel(eCancelCode::TIMEOUT);

					MakeReady(id);
				}
			}
		}

		// 2) ready 실행 (budget 만큼)
		int steps = 0;
		while (steps < budget && !m_readyPQ.empty()) 
		{
			const uint32 id = m_readyPQ.top().id;
			m_readyPQ.pop();

			auto it = m_fibers.find(id);
			if (it == m_fibers.end()) 
				continue;

			Fiber* f = it->second;
			if (f->state != eFiberState::READY) 
				continue;

			BindFls(f);
			m_currentId = id;
			StartRun(f);

			m_backend.SwitchTo(f->ctx); // 파이버 한 스텝 실행 → Yield/Suspend/Terminate 시 메인 복귀

			EndRun(f);
			m_currentId = 0;
			++steps;
			++m_profile.stepCount;

			if (f->state == eFiberState::TERMINATED) 
			{
				m_backend.DestroyFiber(f->ctx);
				Fiber* dead = f;
				m_fibers.erase(id);
				FiberPool::Push(dead);
			}
		}

		// 3) 인박스 한 번 더 비우기(레이턴시↓)
		DrainInbox();
		m_profile.lastPollCostNs = NowNs() - t0;
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
		Fiber* f = it->second;

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

	void FiberScheduler::MakeReady(uint32 id)
	{
		auto it = m_fibers.find(id);
		if (it == m_fibers.end()) 
			return;
		Fiber* f = it->second;
		f->state = eFiberState::READY;
		m_readyPQ.push(ReadyItem{f->priority, m_readySeq++, f->id});
	}

	void FiberScheduler::OnFiberException(uint32_t id, const char* what)
	{
		// todo
	}

	FiberScheduler::Fiber* FiberScheduler::CurrentFiber()
	{
		auto it = m_fibers.find(Current());
		if (it == m_fibers.end()) 
			throw std::runtime_error("No current fiber");
		return it->second;
	}

	void FiberScheduler::BindFls(Fiber* f)
	{
		f->fls.scheduler = this;
		f->fls.fiberId = f->id;
		SetFlsCtx(&f->fls);
	}

	void FiberScheduler::StartRun(Fiber* f)
	{
		++f->switches;
		++m_profile.switchCount;
		f->lastRunStartNs = NowNs();
	}

	void FiberScheduler::EndRun(Fiber* f)
	{
		uint64 now = NowNs();
		if (f->lastRunStartNs)
			f->runNsAcc += (now - f->lastRunStartNs);
		++f->steps;

		uint64 used = 0, total = 0;
		if (WinFiberBackend::ProbeCurrentFiberStack(used, total))
		{
			// todo
		}
	}

	void FiberScheduler::Wake(Fiber* f, eResumeCode rc, eCancelCode cc)
	{
		if (!f)
			return;

		if (f->state == eFiberState::WAITING_EXTERNAL)
		{
			if (f->awaitKey)
			{
				auto w = m_waitMap.find(f->awaitKey);
				if (w != m_waitMap.end() && w->second == f->id)
				{
					m_waitMap.erase(w);
				}
			}
		}

		if (f->cancel)
		{
			f->cancel->RequestCancel(cc == eCancelCode::NONE ? eCancelCode::MANUAL : cc);
		}

		f->resume = rc;
		f->awaitKey = 0;
		MakeReady(f->id);
	}
}
