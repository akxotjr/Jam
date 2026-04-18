#include "pch.h"
#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/MainExecutor.h"

namespace jam
{
	void GlobalEventBus::Unsubscribe(const std::type_index& type, std::size_t id)
	{
		WRITE_LOCK
		auto it = m_handlers.find(type);
		if (it == m_handlers.end()) return;

		auto& vec = it->second;
		erase_if(vec, [id](const Slot& s) { return s.id == id; });

		if (vec.empty()) m_handlers.erase(it);
	}

	std::function<void(Job)> GlobalEventBus::MakeDispatcher(const SubscribeOptions& opt)
	{
		switch (opt.policy)
		{
		case eDispatchPolicy::Immediate:
			return [](Job j) { j.Execute(); };

		case eDispatchPolicy::MainExecutor:
			return [](Job j) { MainExecutor::Instance().Submit(std::move(j)); };

		case eDispatchPolicy::GlobalExecutor:
			return [](Job j) { GlobalExecutor::Instance().Submit(std::move(j)); };

		case eDispatchPolicy::Executor:
			return [exec = opt.executor ? opt.executor : &m_defaultExec](Job j) mutable { exec->Submit(std::move(j)); };
		}
		return [](Job j) { j.Execute(); };
	}
}
