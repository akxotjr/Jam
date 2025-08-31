#pragma once
#include <functional>

namespace jam::utils::job
{
	//using CallbackType = function<void()>;

	//class Job
	//{
	//public:
	//	Job(CallbackType&& callback) : m_callback(std::move(callback)) {}

	//	template<typename T, typename Ret, typename... Args>
	//	Job(std::shared_ptr<T> owner, Ret(T::* memFunc)(Args...), Args&&... args)
	//	{
	//		m_callback = [owner, memFunc, args...]()
	//			{
	//				(owner.get()->*memFunc)(args...);
	//			};
	//	}

	//	void			Execute() { m_callback(); }

	//private:
	//	CallbackType	m_callback;
	//};
    class Job
	{
    public:
        using CallbackType = std::function<void()>;

        Job() = default;

        // 어떤 callable도 받는 기본 생성자
        template<class F>
        Job(F&& f) : m_callback(std::forward<F>(f)) {}

        // 멤버함수 바인딩 (owner를 강소유: 실행 보장)
        template<class T, class MemFn, class... Args,
            std::enable_if_t<std::is_member_function_pointer_v<MemFn>, int> = 0>
        Job(std::shared_ptr<T> owner, MemFn mf, Args&&... args) {
            auto tup = std::make_tuple(std::forward<Args>(args)...);        // perfect forwarding
            m_callback = [owner = std::move(owner), mf, tup = std::move(tup)]() mutable {
                std::apply([&](auto&&... a) {
                    (owner.get()->*mf)(std::forward<decltype(a)>(a)...);
                    }, tup);
                };
        }

        // 멤버함수 바인딩 (owner를 약소유: 종료 중이면 실행 생략)
        template<class T, class MemFn, class... Args>
        static Job Weak(std::weak_ptr<T> owner, MemFn mf, Args&&... args) {
            auto tup = std::make_tuple(std::forward<Args>(args)...);
            return Job([o = std::move(owner), mf, tup = std::move(tup)]() mutable {
                if (auto s = o.lock()) {
                    std::apply([&](auto&&... a) {
                        (s.get()->*mf)(std::forward<decltype(a)>(a)...);
                        }, tup);
                }
                // else: 드랍
                });
        }

        // 예외 처리 정책: 샤드 루프 보호용
        void Execute() noexcept {
            try { if (m_callback) m_callback(); }
            catch (...) {
                // TODO: 로그/메트릭. 릴리즈 빌드에서는 삼키는 게 안전
            }
        }

    private:
        CallbackType m_callback;
    };

}

