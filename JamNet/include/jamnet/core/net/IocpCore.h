#pragma once

namespace jam::net
{
	class IocpObject : public std::enable_shared_from_this<IocpObject>
	{
	public:
		virtual HANDLE	GetHandle() = 0;
		virtual void	Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) = 0;
	};

	class IocpCore
	{
	public:
		IocpCore();
		~IocpCore();

		HANDLE			GetHandle() const { return m_iocpHandle; }

		bool			Register(const shared_ptr<IocpObject>& obj);
		bool			Dispatch(uint32 timeoutMs = INFINITE);

	private:
		HANDLE			m_iocpHandle;
	};
}



