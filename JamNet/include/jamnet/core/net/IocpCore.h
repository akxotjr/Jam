#pragma once

namespace jam::net
{
	class IocpEvent;

	class IocpObject : public std::enable_shared_from_this<IocpObject>
	{
	public:
		virtual ~IocpObject() = default;

		virtual HANDLE	GetHandle() = 0;
		virtual void	Dispatch(IocpEvent* event, int32 bytes = 0) = 0;
	};

	class IocpCore
	{
	public:
		IocpCore();
		~IocpCore();

		HANDLE			GetHandle() const { return m_iocpHandle; }

		bool			Register(const std::shared_ptr<IocpObject>& obj);
		bool			Dispatch(uint32 timeout_ms = INFINITE);
		bool			Post(IocpEvent* event, int32 bytes = 0);
		void			Wake(uint32 count = 1);

	private:
		HANDLE			m_iocpHandle;
	};
}



