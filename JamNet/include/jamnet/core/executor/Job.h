#pragma once


namespace jam
{

    enum class eJobPriority : uint8
    {
        CRITICAL = 0,       // 핸드셰이크, 종료 등 긴급 작업
        CTRL     = 1,       // RPC, 제어 메시지 (기존 CTRL 메일박스)
        NORMAL   = 2,       // 일반 패킷 처리 (기존 NORMAL 메일박스)
        LOW      = 3,       // 백그라운드 정리, 통계 집계
        COUNT    = 4
    };


    class Job
	{
    public:
        Job() = default;

        template<class Fn>
        explicit Job(Fn&& f, const eJobPriority p = eJobPriority::NORMAL) 
    		: m_callback(std::forward<Fn>(f)), m_priority(p) {}

        template<class T, class MemFn, class... Args>
        Job(std::shared_ptr<T> owner, MemFn mf, Args&&... args, const eJobPriority p = eJobPriority::NORMAL) requires (std::is_member_function_pointer_v<MemFn>)
        {
            m_priority = p;
            auto tup = std::make_tuple(std::forward<Args>(args)...);
            m_callback = [o = std::move(owner), mf, t = std::move(tup)]() mutable
                {
                    std::apply([&]<typename... A>(A&&... a) { (o.get()->*mf)(std::forward<A>(a)...); }, t);
                };
        }

        //template<class T, class MemFn, class... Args>
        //static Job Weak(std::weak_ptr<T> owner, MemFn mf, Args&&... args)
        //{
        //    auto tup = std::make_tuple(std::forward<Args>(args)...);
        //    return Job([o = std::move(owner), mf, tup = std::move(tup)]() mutable {
        //        if (auto s = o.lock())
        //        {
        //            std::apply([&](auto&&... a) { (s.get()->*mf)(std::forward<decltype(a)>(a)...); }, tup);
        //        }
        //        });
        //}

        //template<class T, class MemFn, class... Args>
        //static Job Weak(std::weak_ptr<T> owner, MemFn mf, Args&&... args, eJobPriority p = eJobPriority::NORMAL)
        //{
        //    auto tup = std::make_tuple(std::forward<Args>(args)...);
        //    return Job([o = std::move(owner), mf, tup = std::move(tup)]() mutable 
        //        {
	       //         if (auto s = o.lock())
	       //         {
	       //             std::apply([&]<typename... T0>(T0&&... a) { (s.get()->*mf)(std::forward<T0>(a)...); }, tup);
	       //         }
        //        }, p);
        //}

        template<class T, class MemFn, class... Args>
        Job(std::weak_ptr<T> owner, MemFn mf, Args&&... args, const eJobPriority p = eJobPriority::NORMAL)
        {
            m_priority = p;
            auto tup = std::make_tuple(std::forward<Args>(args)...);

            if (auto sp = owner.lock())
            {
                m_callback = [o = std::move(sp), mf, t = std::move(tup)]() mutable
                    {
                        std::apply([&]<typename... A>(A&&... a) { (o.get()->*mf)(std::forward<A>(a)...); }, t);
                    };
            }
            else
            {
                m_callback = nullptr;
            }
        }

        eJobPriority    Priority() const noexcept { return m_priority; }
        void            SetPriority(eJobPriority p) noexcept { m_priority = p; }

        void            Execute() noexcept { if (m_callback) m_callback(); }

    private:
        Callback        m_callback = nullptr;
        eJobPriority    m_priority = eJobPriority::NORMAL;
    };

    struct JobPriorityOf
    {
        eJobPriority operator()(const Job& j) const noexcept { return j.Priority(); }
    };

}

