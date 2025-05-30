#pragma once
#include <functional>

namespace jam::utils::job
{
	using CallbackType = function<void()>;

	USING_SHARED_PTR(Job)

	class Job
	{
	public:
		Job(CallbackType&& callback) : m_callback(std::move(callback)) {}

		template<typename T, typename Ret, typename... Args>
		Job(std::shared_ptr<T> owner, Ret(T::* memFunc)(Args...), Args&&... args)
		{
			m_callback = [owner, memFunc, args...]()
				{
					(owner.get()->*memFunc)(args...);
				};
		}

		void			Execute() { m_callback(); }

	private:
		CallbackType	m_callback;
	};
}

