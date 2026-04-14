#pragma once

#include <typeindex>

#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/IExecutor.h"

namespace jam
{
	class GlobalExecutor;

	enum class eDispatchPolicy
	{
	    Immediate,			// 발행 스레드에서 즉시 실행
		MainExecutor,		// MainExecutor에서 실행
		GlobalExecutor,		// GlobalExecutor에서 실행
	    Executor			// 지정한 Executor(e.g. ShardExecutor)에서 실행
	};

	struct SubscribeOptions
	{
	    eDispatchPolicy policy   = eDispatchPolicy::Immediate;
	    IExecutor*      executor = nullptr; // policy == EXECUTOR 일 때만 사용
	};

	class GlobalEventBus
	{
		DECLARE_SINGLETON(GlobalEventBus)

	public:
	    struct Subscription
		{
	        GlobalEventBus*			bus  = nullptr;
	        std::type_index			type = typeid(void);
	        size_t					id	 = 0;

	        Subscription() = default;
	        Subscription(GlobalEventBus* b, std::type_index t, size_t i) : bus(b), type(t), id(i) {}
	        Subscription(const Subscription&) = delete;
	        Subscription& operator=(const Subscription&) = delete;
	        Subscription(Subscription&& o) noexcept : bus(o.bus), type(o.type), id(o.id) { o.bus = nullptr; o.id = 0; }
	        Subscription& operator=(Subscription&& o) noexcept
	    	{
	            if (this != &o) { reset(); bus = o.bus; type = o.type; id = o.id; o.bus = nullptr; o.id = 0; }
	            return *this;
	        }
	        ~Subscription() { reset(); }
	        void reset()
	    	{
	            if (bus && id) { bus->Unsubscribe(type, id); bus = nullptr; id = 0; }
	        }
	        explicit operator bool() const { return bus != nullptr && id != 0; }
	    };

	    template<typename E>
	    Subscription Subscribe(std::function<void(const E&)> cb, SubscribeOptions opt = {})
		{
	        auto type = std::type_index(typeid(E));
	        auto h = std::make_shared<Handler<E>>(std::move(cb));
	        std::function<void(Job)> dispatch = MakeDispatcher(opt);

	        std::size_t newId;
	        {
				WRITE_LOCK
	            newId = ++m_nextId;
	            m_handlers[type].push_back(Slot{ newId, std::move(h), std::move(dispatch) });
	        }
	        return Subscription{ this, type, newId };
	    }

	    template<typename E>
	    void Publish(E&& ev) 
		{
	        using EvT = std::decay_t<E>;
	        auto evp = std::make_shared<EvT>(std::move(ev)); // 이벤트 수명 보장

	        std::vector<SlotView> snapshot;
	        {
				READ_LOCK
	            auto it = m_handlers.find(std::type_index(typeid(EvT)));
	            if (it == m_handlers.end()) return;
	            snapshot.reserve(it->second.size());
	            for (auto& s : it->second) 
				{
	                snapshot.push_back(SlotView{ s.h, s.dispatch });
	            }
	        }

	        // 잠금 없이 디스패치(Job으로 래핑)
			for (auto& sv : snapshot) 
			{
				auto hb = sv.h;
				auto evpCopy = evp; // 각 작업이 shared_ptr을 소유
				Job job([hb, evpCopy]() {
					hb->invoke(static_cast<const void*>(evpCopy.get()));
					});
				sv.dispatch(std::move(job));
			}
	    }


		void Unsubscribe(const std::type_index& type, std::size_t id);


	private:
		std::function<void(Job)> MakeDispatcher(const SubscribeOptions& opt);


	private:
	    struct HandlerBase
		{
	        virtual ~HandlerBase() = default;
	        virtual void invoke(const void* ev) = 0;
	    };
	    template<typename E>
	    struct Handler : HandlerBase
		{
	        std::function<void(const E&)> cb;
	        explicit Handler(std::function<void(const E&)> f) : cb(std::move(f)) {}
	        void invoke(const void* ev) override { cb(*static_cast<const E*>(ev)); }
	    };

	    struct Slot
		{
	        size_t							id		 = 0;
	        std::shared_ptr<HandlerBase>	h		 = nullptr;
	        std::function<void(Job)>		dispatch = nullptr;
	    };
	    struct SlotView
		{
	        std::shared_ptr<HandlerBase>	h		 = nullptr;
	        std::function<void(Job)>		dispatch = nullptr;
	    };


	    // 기본 Executor(아무것도 지정 안했을 때 IMMEDIATE와 동일)
	    struct InlineExecutor : IExecutor
		{
	        void Submit(Job j) override { j.Execute(); }
	    };



		USE_LOCK
			
		InlineExecutor											m_defaultExec;
		std::unordered_map<std::type_index, std::vector<Slot>>	m_handlers;
	    size_t													m_nextId		= 0;
	};

} 


#define GLOBAL_EVENTBUS							jam::GlobalEventBus::Instance()
#define GLOBAL_EVENTBUS_SUBSCRIBE(E, CB, OPT)	GLOBAL_EVENTBUS.Subscribe<E>(CB, OPT)
#define GLOBAL_EVENTBUS_UNSUBSCRIBE(TYPE, ID)	GLOBAL_EVENTBUS.Unsubscribe(TYPE, ID)
#define GLOBAL_EVENTBUS_PUBLISH(EV)				GLOBAL_EVENTBUS.Publish(EV)
