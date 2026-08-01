#pragma once

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>


namespace jam
{
	class Callback
	{
		struct ICallable
		{
			virtual ~ICallable() = default;
			virtual void Invoke() = 0;
		};

		template<typename Fn>
		struct Callable final : ICallable
		{
			template<typename F>
			explicit Callable(F&& fn)
				: m_fn(std::forward<F>(fn)) {}

			void Invoke() override
			{
				m_fn();
			}

			Fn m_fn;
		};

	public:
		Callback() = default;
		Callback(std::nullptr_t) {}

		template<typename Fn>
		Callback(Fn&& fn)
		{
			Assign(std::forward<Fn>(fn));
		}

		Callback(Callback&&) noexcept = default;
		Callback& operator=(Callback&&) noexcept = default;
		Callback(const Callback&) = delete;
		Callback& operator=(const Callback&) = delete;

		template<typename Fn>
		Callback& operator=(Fn&& fn)
		{
			Assign(std::forward<Fn>(fn));
			return *this;
		}

		Callback& operator=(std::nullptr_t)
		{
			m_callable.reset();
			return *this;
		}

		explicit operator bool() const noexcept
		{
			return static_cast<bool>(m_callable);
		}

		void operator()()
		{
			if (m_callable)
				m_callable->Invoke();
		}

	private:
		template<typename Fn>
		void Assign(Fn&& fn)
		{
			using FnT = std::decay_t<Fn>;
			m_callable = std::make_unique<Callable<FnT>>(std::forward<Fn>(fn));
		}

	private:
		std::unique_ptr<ICallable> m_callable = nullptr;
	};

	enum class eJobPriority : uint8
	{
		Critical = 0,       // 핸드셰이크, 종료 등 긴급 작업
		Control  = 1,       // RPC, 제어 메시지 (기존 CTRL 메일박스)
		Normal   = 2,       // 일반 패킷 처리 (기존 NORMAL 메일박스)
		Low      = 3,       // 백그라운드 정리, 통계 집계

		Count
	};
	inline constexpr size_t k_jobPriorityCount = static_cast<size_t>(eJobPriority::Count);

	class Job
	{
	public:
		Job() = default;
		Job(Job&&) noexcept = default;
		Job& operator=(Job&&) noexcept = default;
		Job(const Job&) noexcept = default;
		Job& operator=(const Job&) noexcept = default;

		template<class Fn>
		explicit Job(Fn&& f, const eJobPriority p = eJobPriority::Normal)
			: m_callback(std::make_shared<Callback>(std::forward<Fn>(f))), m_priority(p) {}

		template<class T, class MemFn, class... Args>
		Job(std::shared_ptr<T> owner, MemFn mf, const eJobPriority p = eJobPriority::Normal, Args&&... args) requires (std::is_member_function_pointer_v<MemFn>)
		{
			m_priority = p;
			auto tup = std::make_tuple(std::forward<Args>(args)...);
			m_callback = std::make_shared<Callback>(
				[o = std::move(owner), mf, t = std::move(tup)]() mutable
				{
					std::apply([&]<typename... A>(A&&... a) { (o.get()->*mf)(std::forward<A>(a)...); }, t);
				});
		}

		template<class T, class MemFn, class... Args>
		Job(std::weak_ptr<T> owner, MemFn mf, const eJobPriority p = eJobPriority::Normal, Args&&... args) requires (std::is_member_function_pointer_v<MemFn>)
		{
			m_priority = p;
			auto tup = std::make_tuple(std::forward<Args>(args)...);
			m_callback = std::make_shared<Callback>(
				[w = std::move(owner), mf, t = std::move(tup)]() mutable
				{
					if (auto o = w.lock())
					{
						std::apply([&]<typename... A>(A&&... a) { (o.get()->*mf)(std::forward<A>(a)...); }, t);
					}
				});
		}

		eJobPriority    Priority() const noexcept { return m_priority; }
		void            SetPriority(eJobPriority p) noexcept { m_priority = p; }

		void            Execute() noexcept { if (m_callback) (*m_callback)(); }

	private:
		std::shared_ptr<Callback> m_callback = nullptr;
		eJobPriority              m_priority = eJobPriority::Normal;
	};

	struct JobPriorityOf
	{
		eJobPriority operator()(const Job& j) const noexcept { return j.Priority(); }
	};
}
