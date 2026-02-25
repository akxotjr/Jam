#include "pch.h"
#include "jamnet/core/executor/GlobalEventBus.h"

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
		case eDispatchPolicy::IMMEDIATE:
			return [](Job j) { j.Execute(); };
		case eDispatchPolicy::MAIN_EXECUTOR:
			return [](Job j) {
				MainExecutor::Instance().Submit(std::move(j));
				};
		case eDispatchPolicy::GLOBAL_EXECUTOR:
			return [](Job j) {
				GlobalExecutor::Instance().Submit(std::move(j));
				};
		case eDispatchPolicy::EXECUTOR:
			return [exec = opt.executor ? opt.executor : &m_defaultExec](Job j) mutable {
				exec->Submit(std::move(j));
				};
		}
		return [](Job j) { j.Execute(); };
	}
}
