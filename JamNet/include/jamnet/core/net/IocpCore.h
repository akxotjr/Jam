#pragma once
#include <atomic>

namespace jam::net
{
	struct IocpEvent;

	class IocpObject : public std::enable_shared_from_this<IocpObject>
	{
	public:
		virtual ~IocpObject() = default;

		virtual HANDLE	GetHandle() = 0;
		virtual void	Dispatch(IocpEvent* event, int32 bytes = 0) = 0;
		virtual void	OnPendingDispatchDrained() {}

		bool			TryAddPendingDispatch();

		void			ReleasePendingDispatch() { m_pendingDispatchCount.fetch_sub(1, std::memory_order_acq_rel); }
		void			MarkClosing() { m_closing.store(true, std::memory_order_release); }
		bool			IsClosing() const { return m_closing.load(std::memory_order_acquire); }
		int32			GetPendingDispatchCount() const { return m_pendingDispatchCount.load(std::memory_order_acquire); }

	private:
		std::atomic<int32>	m_pendingDispatchCount = 0;
		std::atomic<bool>	m_closing			   = false;
	};

	class IocpCore
	{
	public:
		IocpCore();
		~IocpCore();

		HANDLE			GetHandle() const { return m_iocpHandle; }

		bool			Register(IocpObject* obj);
		bool			Dispatch(uint32 timeout_ms = INFINITE);
		bool			Post(const IocpObject* obj, IocpEvent* event, int32 bytes = 0);
		void			Wake(uint32 count = 1);

	private:
		HANDLE			m_iocpHandle;
	};
}
