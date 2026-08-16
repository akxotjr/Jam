#include "pch.h"
#include "jamnet/core/net/WinErrorHandling.h"

namespace jam::net
{
	namespace win_error
	{
		namespace
		{
			using RtlNtStatusToDosErrorFn = ULONG (NTAPI*)(LONG);

			RtlNtStatusToDosErrorFn ResolveRtlNtStatusToDosError()
			{
				static RtlNtStatusToDosErrorFn fn = []() -> RtlNtStatusToDosErrorFn
				{
					HMODULE module = ::GetModuleHandleW(L"ntdll.dll");
					if (!module)
						module = ::LoadLibraryW(L"ntdll.dll");
					if (!module)
						return nullptr;

					return reinterpret_cast<RtlNtStatusToDosErrorFn>(
						::GetProcAddress(module, "RtlNtStatusToDosError"));
				}();

				return fn;
			}

			LONG ToNativeStatus(ULONG_PTR status)
			{
				return static_cast<LONG>(status);
			}

			std::string GetWinErrorMessage(DWORD errorCode)
			{
				LPSTR buffer = nullptr;
				const DWORD len = ::FormatMessageA(
					FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					nullptr,
					errorCode,
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					reinterpret_cast<LPSTR>(&buffer),
					0,
					nullptr);

				if (len == 0 || !buffer)
					return {};

				std::string message(buffer, len);
				::LocalFree(buffer);

				while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
					message.pop_back();

				return message;
			}
		}

		DWORD GetLastWinError()
		{
			return ::GetLastError();
		}

		int32 GetLastWsaError()
		{
			return ::WSAGetLastError();
		}

		bool IsIoPending(int32 errorCode)
		{
			return errorCode == WSA_IO_PENDING;
		}

		ULONG_PTR GetOverlappedNativeStatus(const OVERLAPPED& overlapped)
		{
			return overlapped.Internal;
		}

		bool IsNativeStatusSuccess(ULONG_PTR status)
		{
			return ToNativeStatus(status) >= 0;
		}

		DWORD NativeStatusToWinError(ULONG_PTR status)
		{
			if (IsNativeStatusSuccess(status))
				return ERROR_SUCCESS;

			if (auto* fn = ResolveRtlNtStatusToDosError())
				return static_cast<DWORD>(fn(ToNativeStatus(status)));

			return ERROR_GEN_FAILURE;
		}

		DWORD GetOverlappedWinError(const OVERLAPPED& overlapped)
		{
			return NativeStatusToWinError(GetOverlappedNativeStatus(overlapped));
		}

		void LogWinError(std::string_view context, DWORD errorCode)
		{
			const std::string message = GetWinErrorMessage(errorCode);
			if (message.empty())
				JAM_LOG_ERROR("{} failed. ec={}", context, errorCode);
			else
				JAM_LOG_ERROR("{} failed. ec={}, msg={}", context, errorCode, message);
		}

		void LogWsaError(std::string_view context, int32 errorCode)
		{
			LogWinError(context, static_cast<DWORD>(errorCode));
		}

		void LogNativeStatus(std::string_view context, ULONG_PTR status)
		{
			const DWORD errorCode = NativeStatusToWinError(status);
			const std::string message = GetWinErrorMessage(errorCode);
			if (message.empty())
				JAM_LOG_ERROR("{} failed. status=0x{:X}, ec={}", context, static_cast<uint32>(status), errorCode);
			else
				JAM_LOG_ERROR("{} failed. status=0x{:X}, ec={}, msg={}", context, static_cast<uint32>(status), errorCode, message);
		}

		void LogLastWinError(std::string_view context)
		{
			LogWinError(context, GetLastWinError());
		}

		void LogLastWsaError(std::string_view context)
		{
			LogWsaError(context, GetLastWsaError());
		}
	}
}
