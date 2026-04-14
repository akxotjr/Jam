#include "pch.h"
#include "jamnet/core/executor/FiberScheduler.h"
#include "jamnet/core/executor/ConcurrentQueueToken.h"
#include <algorithm>


namespace jam
{
	FiberScheduler::FiberScheduler(WinFiberBackend& backend)
		: m_backend(backend),
		m_resumeCtok(m_resumeInbox),
		m_spawnCtok(m_spawnInbox),
		m_cancelKeyCtok(m_cancelKeyInbox),
		m_cancelIdCtok(m_cancelIdInbox)
	{
	}

	void FiberScheduler::AttachToCurrentThread()
	{
		const std::thread::id currentThreadId = std::this_thread::get_id();
		if (m_ownerThreadId == std::thread::id{})
			m_ownerThreadId = currentThreadId;

		JAM_ASSERT(m_ownerThreadId == currentThreadId);

		m_main = m_backend.ConvertThreadToMainFiber();
		EnsureFlsKey();
		m_mainCtx.scheduler = this;
		m_mainCtx.fiberId	= 0;
		SetFiberContext(&m_mainCtx);
	}

	void FiberScheduler::DetachFromThread()
	{
		JAM_ASSERT(m_ownerThreadId == std::this_thread::get_id());
		SetFiberContext(nullptr);
		m_backend.RevertMainFiber(m_main);
		m_main = nullptr;
		m_ownerThreadId = std::thread::id{};
	}

	uint32 FiberScheduler::SpawnFiber(FiberFn fn, const FiberDesc& desc)
	{
		const uint32 id = m_nextId++;

		Fiber* f = FiberPool::Pop();
		*f = Fiber{};
		
		f->id			= id;
		f->name			= desc.name;
		f->reserve		= desc.stackReserve;
		f->commit		= desc.stackCommit;
		f->state		= eFiberState::Ready;
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
		f->state = eFiberState::Ready;
		MakeReady(f->id);
		ProbeFiberStack(f);
		m_backend.SwitchTo(m_main);
	}

	void FiberScheduler::SleepUntil(uint64 wakeup_ns)
	{
		Fiber* f = CurrentFiber();
		f->state	 = eFiberState::WatingTimer;
		f->wakeup_ns = wakeup_ns;
		m_sleepPQ.push({ wakeup_ns, f->id, ++f->sleepGeneration });
		ProbeFiberStack(f);
		m_backend.SwitchTo(m_main);
	}

	bool FiberScheduler::Suspend(FiberAwaitKey key, uint64 deadline_ns)
	{
		Fiber* f = CurrentFiber();
		f->state		= eFiberState::WatingExternal;
		f->awaitKey		= key;
		f->resume		= eResumeCode::None;
		f->deadline_ns	= deadline_ns;
		m_waitMap.emplace(key, f->id);

		if (deadline_ns) 
		{
			m_sleepPQ.push({ deadline_ns, f->id, ++f->sleepGeneration });
		}
		ProbeFiberStack(f);
		m_backend.SwitchTo(m_main); // 재개되면 아래로 이어짐
		return (f->resume == eResumeCode::Signaled);
	}

	bool FiberScheduler::Resume(FiberAwaitKey key)
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
		if (f->state != eFiberState::WatingExternal) 
			return false;

		f->awaitKey	   = 0;
		f->deadline_ns = 0_ns;
		++f->sleepGeneration;
		f->resume	   = eResumeCode::Signaled;
		MakeReady(id);
		return true;
	}

	bool FiberScheduler::CancelByKey(FiberAwaitKey key, eCancelCode code)
	{
		auto it = m_waitMap.find(key);
		if (it == m_waitMap.end()) return false;

		auto fit = m_fibers.find(it->second);
		if (fit == m_fibers.end()) return false;

		Fiber* f = fit->second;
		CompleteAwait(f, eResumeCode::Cancelled, code);
		return true;
	}

	bool FiberScheduler::CancelById(uint32 id, eCancelCode code)
	{
		auto fit = m_fibers.find(id);
		if (fit == m_fibers.end()) return false;

		Fiber* f = fit->second;
		CompleteAwait(f, eResumeCode::Cancelled, code);
		return true;
 	}

	void FiberScheduler::PostResume(FiberAwaitKey key)
	{
		auto& ptok = TlsTokenFor(m_resumeInbox);
		m_resumeInbox.enqueue(ptok, ResumeMsg{ key });
	}

	void FiberScheduler::PostSpawn(FiberFn fn, const FiberDesc& desc)
	{
		auto& ptok = TlsTokenFor(m_spawnInbox);
		m_spawnInbox.enqueue(ptok, SpawnMsg{ .fn = std::move(fn), .desc = desc });
	}

	void FiberScheduler::PostCancelByKey(FiberAwaitKey key, eCancelCode code)
	{
		auto& ptok = TlsTokenFor(m_cancelKeyInbox);
		m_cancelKeyInbox.enqueue(ptok, CancelKeyMsg{ .key = key, .code = code });
	}

	void FiberScheduler::PostCancelById(uint32 id, eCancelCode code)
	{
		auto& ptok = TlsTokenFor(m_cancelIdInbox);
		m_cancelIdInbox.enqueue(ptok, CancelIdMsg{ .id = id, .code = code });
	}

	void FiberScheduler::DrainInbox()
	{
		ResumeMsg r;
		while (m_resumeInbox.try_dequeue(m_resumeCtok, r))
		{
			Resume(r.key);
			++m_metrics.inboxResumeCount;
		}
		SpawnMsg s;
		while (m_spawnInbox.try_dequeue(m_spawnCtok, s))
		{
			SpawnFiber(std::move(s.fn), s.desc);
           ++m_metrics.inboxSpawnCount;
		}
		CancelKeyMsg ck;
		while (m_cancelKeyInbox.try_dequeue(m_cancelKeyCtok, ck))
		{
			CancelByKey(ck.key, ck.code);
			++m_metrics.inboxCancelByKeyCount;
		}
		CancelIdMsg ci;
		while (m_cancelIdInbox.try_dequeue(m_cancelIdCtok, ci))
		{
			CancelById(ci.id, ci.code);
			++m_metrics.inboxCancelByIdCount;
		}
	}

	void FiberScheduler::Poll(int32 budget, uint64 now_ns)
	{
		const uint64 pollStart_ns = NOW_NS();
		++m_metrics.pollCount;

		// 0) 인박스 먼저 처리
		DrainInbox();

		// 1) 타이머/타임아웃 기상
		WakeupTimed(now_ns);

		// 2) ready 실행 (budget 만큼)
		int32 steps = 0;
        m_metrics.lastPollReadyRunCount = 0;
		while (steps < budget && !m_readyPQ.empty()) 
		{
			const uint32 id = m_readyPQ.top().id;
			m_readyPQ.pop();

			auto it = m_fibers.find(id);
			if (it == m_fibers.end()) 
				continue;

			Fiber* f = it->second;

			// READY가 아니거나(중간에 상태 바뀜), 이미 다른 pop에서 소비된 stale 엔트리라면 스킵
			if (f->state != eFiberState::Ready || !f->inReadyQ)
				continue;

			//  이 엔트리는 이 실행에서 소비됨
			f->inReadyQ = false;

			BindFls(f);
			m_currentId = id;
			StartRun(f);

			m_backend.SwitchTo(f->ctx); // 파이버 한 스텝 실행 -> Yield/Suspend/Terminate 시 메인 복귀
			SetFiberContext(&m_mainCtx);

			EndRun(f);
			m_currentId = 0;
			++steps;
            ++m_metrics.readyRunCount;
            ++m_metrics.lastPollReadyRunCount;
			++m_metrics.stepCount;

			if (f->state == eFiberState::Terminated) 
			{
				m_backend.DestroyFiber(f->ctx);
				Fiber* dead = f;
				m_fibers.erase(id);
				FiberPool::Push(dead);
			}
		}

		// 3) 실행 중 경과된 시간 기준으로 한 번 더 기상
		WakeupTimed(NOW_NS());

		// 3) 인박스 한 번 더 비우기(레이턴시↓)
		DrainInbox();
		const uint64 pollEnd_ns = NOW_NS();
		m_metrics.lastPollCost_ns = pollEnd_ns - pollStart_ns;
        m_metrics.pollCostAcc_ns += m_metrics.lastPollCost_ns;

		if (steps == 0)
			++m_metrics.emptyPollCount;
	}

	uint64 FiberScheduler::NextWakeupTime() const
	{
		return m_sleepPQ.empty() ? 0_ns : m_sleepPQ.top().wakeup_ns;
	}

	uint32 FiberScheduler::Current() const
	{
		if (auto* c = GetFiberContext())
			return c->fiberId;
		return 0;
	}

	void FiberScheduler::Trampoline(void* p)
	{
		auto* prm = reinterpret_cast<TrampolineParam*>(p);
		auto* self = prm->self;
		const uint32 id = prm->id;

		Fiber* f = nullptr;
		{   // 이 블록 안에서만 iterator를 쓰고 파이버 유저코드 전에 파괴한다
			auto it = self->m_fibers.find(id);
			if (it == self->m_fibers.end()) {
				self->m_backend.SwitchTo(self->m_main);
				return;
			}
			f = it->second;   // 원시 포인터만 들고 나감
		}

		self->BindFls(f);

		try {
			f->entry();
		}
		catch (const std::exception& e) {
			//std::cout << "Fiber Exception: " << e.what() << std::endl;
			self->OnFiberException(id, e.what());
		}
		catch (...) {
			//std::cout << "Fiber Unknown Exception" << std::endl;
			self->OnFiberException(id, "unknown exception");
		}

		f->state = eFiberState::Terminated;
		self->ProbeFiberStack(f);
		self->m_backend.SwitchTo(self->m_main);
	}

	void FiberScheduler::MakeReady(uint32 id)
	{
		auto it = m_fibers.find(id);
		if (it == m_fibers.end()) 
			return;

		Fiber* f = it->second;
		if (f->state == eFiberState::Ready && f->inReadyQ)
			return;

		f->state    = eFiberState::Ready;
		f->inReadyQ = true;
		m_readyPQ.push(ReadyItem{f->priority, m_readySeq++, f->id});
	}

	void FiberScheduler::OnFiberException(uint32_t id, const char* what)
	{
		JAMNET_LOG_CRITICAL_LOC("Fiber Exeception id= {}, what= {}", id, what);
	}

	FiberScheduler::Fiber* FiberScheduler::CurrentFiber()
	{
		uint32 id = Current();
		if (id == 0)
		{
			JAMNET_LOG_CRITICAL_LOC("CurrentFiber() called outside fiber! Check call stack!");
			throw std::runtime_error("No current fiber");
		}

		auto it = m_fibers.find(id);
		if (it == m_fibers.end())
			throw std::runtime_error("Fiber not found: " + std::to_string(id));
		return it->second;
	}

	void FiberScheduler::BindFls(Fiber* f)
	{
		f->fiberContext.scheduler = this;
		f->fiberContext.fiberId   = f->id;
		SetFiberContext(&f->fiberContext);
	}

	void FiberScheduler::StartRun(Fiber* f)
	{
		++f->switches;
		++m_metrics.switchCount;
		f->lastRunStart_ns = NOW_NS();
	}

	void FiberScheduler::EndRun(Fiber* f)
	{
		uint64 now_ns = NOW_NS();
		if (f->lastRunStart_ns)
			f->runtimeAcc_ns += (now_ns - f->lastRunStart_ns);
		++f->steps;
	}

	void FiberScheduler::ProbeFiberStack(Fiber* f)
	{
		uint64 used = 0, total = 0;
		if (!f || !WinFiberBackend::ProbeCurrentFiberStack(used, total))
			return;

		f->stackUsed  = used;
		f->stackTotal = total;
		f->stackPeak  = std::max(f->stackPeak, used);

		m_metrics.lastStackUsed  = used;
		m_metrics.lastStackTotal = total;
		m_metrics.peakStackUsed  = std::max(m_metrics.peakStackUsed, used);

		if (total == 0)
			return;

		const uint64 usagePermille = used * 1000 / total;
		if (usagePermille >= 900)
		{
			JAMNET_LOG_CRITICAL_LOC("Fiber stack usage is critical. id={}, name={}, used={}, total={}", f->id, f->name ? f->name : "", used, total);
		}
		else if (usagePermille >= 800)
		{
			JAMNET_LOG_WARN_LOC("Fiber stack usage is high. id={}, name={}, used={}, total={}", f->id, f->name ? f->name : "", used, total);
		}
	}

	void FiberScheduler::CompleteAwait(Fiber* f, eResumeCode rc, eCancelCode cc)
	{
		if (!f) return;

		if (f->state == eFiberState::WatingExternal)
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
			f->cancel->RequestCancel(cc == eCancelCode::None ? eCancelCode::Manual : cc);
		}

		f->resume      = rc;
		f->awaitKey    = 0;
		f->wakeup_ns   = 0_ns;
		f->deadline_ns = 0_ns;
		++f->sleepGeneration;
		MakeReady(f->id);
	}

	void FiberScheduler::WakeupTimed(uint64 wakeup_ns)
	{
		while (!m_sleepPQ.empty() && m_sleepPQ.top().wakeup_ns <= wakeup_ns)
		{
			const auto& [wake, id, gen] = m_sleepPQ.top();
			//const uint64 wake = wake;
			//const uint32 id   = fiberId;
			//const uint64 gen  = generation;

			m_sleepPQ.pop();

			auto it = m_fibers.find(id);
			if (it == m_fibers.end()) continue;

			Fiber* f = it->second;
			if (!f) continue;
			if (gen != f->sleepGeneration)
				continue;

			if (f->state == eFiberState::WatingTimer)
			{
				if (wake != f->wakeup_ns)
					continue;
				f->wakeup_ns = 0_ns;
                ++m_metrics.wakeupTimerCount;
				MakeReady(id);
			}
			else if (f->state == eFiberState::WatingExternal)
			{
				if (wake != f->deadline_ns)
					continue;

				if (f->awaitKey)
				{
					auto w = m_waitMap.find(f->awaitKey);
					if (w != m_waitMap.end() && w->second == id)
						m_waitMap.erase(w);

					f->resume      = eResumeCode::Timeout;
					f->awaitKey    = 0;
					f->deadline_ns = 0_ns;

					if (f->cancel) 
						f->cancel->RequestCancel(eCancelCode::Timeout);

                    ++m_metrics.wakeupTimeoutCount;
					MakeReady(id);
				}
			}
			else
			{
				std::cout << "  -> unexpected state, ignored\n";
			}
		}
	}
}
