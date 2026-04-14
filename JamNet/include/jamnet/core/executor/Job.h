#pragma once


namespace jam
{

    enum class eJobPriority : uint8
    {
        Critical = 0,       // 핸드셰이크, 종료 등 긴급 작업
        Control  = 1,       // RPC, 제어 메시지 (기존 CTRL 메일박스)
        Normal   = 2,       // 일반 패킷 처리 (기존 NORMAL 메일박스)
        Low      = 3,       // 백그라운드 정리, 통계 집계
        
    	Count
    };


    class Job
	{
    public:
        Job() = default;

        template<class Fn>
        explicit Job(Fn&& f, const eJobPriority p = eJobPriority::Normal) 
    		: m_callback(std::forward<Fn>(f)), m_priority(p) {}

        template<class T, class MemFn, class... Args>
        Job(std::shared_ptr<T> owner, MemFn mf, Args&&... args, const eJobPriority p = eJobPriority::Normal) requires (std::is_member_function_pointer_v<MemFn>)
        {
            m_priority = p;
            auto tup = std::make_tuple(std::forward<Args>(args)...);
            m_callback = [o = std::move(owner), mf, t = std::move(tup)]() mutable
                {
                    std::apply([&]<typename... A>(A&&... a) { (o.get()->*mf)(std::forward<A>(a)...); }, t);
                };
        }

        template<class T, class MemFn, class... Args>
        Job(std::weak_ptr<T> owner, MemFn mf, Args&&... args, const eJobPriority p = eJobPriority::Normal) requires (std::is_member_function_pointer_v<MemFn>)
        {
            m_priority = p;
            auto tup = std::make_tuple(std::forward<Args>(args)...);
            m_callback = [w = std::move(owner), mf, t = std::move(tup)]() mutable
                {
                    if (auto o = w.lock())
                    {
                        std::apply([&]<typename... A>(A&&... a) { (o.get()->*mf)(std::forward<A>(a)...); }, t);
                    }
                };
        }

        eJobPriority    Priority() const noexcept { return m_priority; }
        void            SetPriority(eJobPriority p) noexcept { m_priority = p; }

        void            Execute() noexcept { if (m_callback) m_callback(); }

    private:
        Callback        m_callback = nullptr;
        eJobPriority    m_priority = eJobPriority::Normal;
    };

    struct JobPriorityOf
    {
        eJobPriority operator()(const Job& j) const noexcept { return j.Priority(); }
    };

}

