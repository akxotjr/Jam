#include "pch.h"
#include "FiberScheduler.h"

#include "Clock.h"

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

	void FiberScheduler::SleepUntil(uint64 wakeup_ns)
	{
		Fiber* f = CurrentFiber();
		f->state = eFiberState::WAITING_TIMER;
		f->wakeup_ns = wakeup_ns;
		m_sleepPQ.push({ wakeup_ns, f->id });
		m_backend.SwitchTo(m_main);
	}

	bool FiberScheduler::Suspend(AwaitKey key, uint64 deadline_ns)
	{
		Fiber* f = CurrentFiber();
		f->state = eFiberState::WAITING_EXTERNAL;
		f->awaitKey = key;
		f->resume = eResumeCode::NONE;
		m_waitMap.emplace(key, f->id);

		if (deadline_ns) 
		{
			f->deadline_ns = deadline_ns;
			m_sleepPQ.push({ deadline_ns, f->id });
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
		CompleteAwait(f, eResumeCode::CANCELLED, code);
		return true;
	}

	bool FiberScheduler::CancelById(uint32 id, eCancelCode code)
	{
		auto fit = m_fibers.find(id);
		if (fit == m_fibers.end()) return false;

		Fiber* f = fit->second;
		CompleteAwait(f, eResumeCode::CANCELLED, code);
		return true;
 	}

	void FiberScheduler::PostResume(AwaitKey key)
	{
		static thread_local std::unique_ptr<moodycamel::ProducerToken> tl_resumePtok;
		if (!tl_resumePtok)
			tl_resumePtok = std::make_unique<moodycamel::ProducerToken>(m_resumeInbox);

		m_resumeInbox.enqueue(*tl_resumePtok, ResumeMsg{ key });
	}

	void FiberScheduler::PostSpawn(FiberFn fn, FiberDesc desc)
	{
		static thread_local std::unique_ptr<moodycamel::ProducerToken> tl_spawnPtok;
		if (!tl_spawnPtok)
			tl_spawnPtok = std::make_unique<moodycamel::ProducerToken>(m_spawnInbox);	// resumeInbox 와 spawnInbox 의 구분없이 같은 producer token을 만들어도 되나??

		m_spawnInbox.enqueue(*tl_spawnPtok, SpawnMsg{ std::move(fn), desc });
	}

	void FiberScheduler::PostCancelByKey(AwaitKey key, eCancelCode code)
	{
		static thread_local std::unique_ptr<moodycamel::ProducerToken> tl_cancelKeyPtok;
		if (!tl_cancelKeyPtok)
			tl_cancelKeyPtok = std::make_unique<moodycamel::ProducerToken>(m_cancelKeyInbox);

		m_cancelKeyInbox.enqueue(*tl_cancelKeyPtok, CancelKeyMsg{ key, code });
	}

	void FiberScheduler::PostCancelById(uint32 id, eCancelCode code)
	{
		static thread_local std::unique_ptr<moodycamel::ProducerToken> tl_cancelIdPtok;
		if (!tl_cancelIdPtok)
			tl_cancelIdPtok = std::make_unique<moodycamel::ProducerToken>(m_cancelIdInbox);

		m_cancelIdInbox.enqueue(*tl_cancelIdPtok, CancelIdMsg{ id, code });
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

	void FiberScheduler::Poll(int32 budget, uint64 now_ns)
	{
		const uint64 pollStart_ns = Clock::Instance().NowNs();

		// 0) 인박스 먼저 처리
		DrainInbox();

		// 1) 타이머/타임아웃 기상
		WakeupTimed(now_ns);

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

			// READY가 아니거나(중간에 상태 바뀜), 이미 다른 pop에서 소비된 stale 엔트리라면 스킵
			if (f->state != eFiberState::READY || !f->inReadyQ)
				continue;

			// ★ 이제 이 엔트리는 이 실행에서 소비됨
			f->inReadyQ = false;

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

		// 3) 실행 중 경과된 시간 기준으로 한 번 더 기상
		WakeupTimed(Clock::Instance().NowNs());

		// 3) 인박스 한 번 더 비우기(레이턴시↓)
		DrainInbox();
		const uint64 pollEnd_ns = Clock::Instance().NowNs();
		m_profile.lastPollCost_ns = pollEnd_ns - pollStart_ns;
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
		if (f->state == eFiberState::READY && f->inReadyQ)
			return; // 이미 큐에 있음

		f->state = eFiberState::READY;
		f->inReadyQ = true;
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
		f->lastRunStart_ns = Clock::Instance().NowNs();
	}

	void FiberScheduler::EndRun(Fiber* f)
	{
		uint64 now_ns = Clock::Instance().NowNs();
		if (f->lastRunStart_ns)
			f->runtimeAcc_ns += (now_ns - f->lastRunStart_ns);
		++f->steps;

		uint64 used = 0, total = 0;
		if (WinFiberBackend::ProbeCurrentFiberStack(used, total))
		{
			// todo
		}
	}

	void FiberScheduler::CompleteAwait(Fiber* f, eResumeCode rc, eCancelCode cc)
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

	void FiberScheduler::WakeupTimed(uint64 wakeup_ns)
	{
		while (!m_sleepPQ.empty() && m_sleepPQ.top().wakeup_ns <= wakeup_ns)
		{
			auto& top = m_sleepPQ.top();
			const uint32 id = top.fiberId;
			m_sleepPQ.pop();
			auto it = m_fibers.find(id);
			if (it == m_fibers.end())
				continue;

			Fiber* f = it->second;
			if (!f) continue;

			if (f->state == eFiberState::WAITING_TIMER)
			{
				if (top.wakeup_ns != f->wakeup_ns) continue;
				f->wakeup_ns = 0;
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
	}
}
