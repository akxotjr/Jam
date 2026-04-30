#pragma once

#include <string_view>

namespace jam::net
{
	namespace win_error
	{
		DWORD		GetLastWinError();
		int32		GetLastWsaError();
		bool		IsIoPending(int32 errorCode);

		ULONG_PTR	GetOverlappedNativeStatus(const OVERLAPPED& overlapped);
		bool		IsNativeStatusSuccess(ULONG_PTR status);
		DWORD		NativeStatusToWinError(ULONG_PTR status);
		DWORD		GetOverlappedWinError(const OVERLAPPED& overlapped);

		void		LogWinError(std::string_view context, DWORD errorCode);
		void		LogWsaError(std::string_view context, int32 errorCode);
		void		LogNativeStatus(std::string_view context, ULONG_PTR status);
		void		LogLastWinError(std::string_view context);
		void		LogLastWsaError(std::string_view context);
	}
}
