#pragma once

namespace jam::net
{
	class IocpObject
	{
	public:
		virtual HANDLE GetHandle() = 0;
		virtual void Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) = 0;
	};

	class IocpCore
	{
	public:
		IocpCore();
		~IocpCore();

		HANDLE	GetHandle() { return _iocpHandle; }

		bool	Register(IocpObjectRef iocpObject);
		bool	Dispatch(uint32 timeoutMs = INFINITE);

	private:
		HANDLE	_iocpHandle;
	};
}



