#pragma once

#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include <jambase/Logger.h>

#if defined(NDEBUG)
#define JAM_DEBUG 0
#else
#define JAM_DEBUG 1
#endif

#if defined(_MSC_VER)
#define JAM_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#define JAM_DEBUG_BREAK() __builtin_trap()
#else
#define JAM_DEBUG_BREAK() std::abort()
#endif

namespace jam::detail
{
	inline void OnAssertFailure(const char* expr, const char* file, int line, const char* func) noexcept
	{
		JAM_LOG_FATAL("Assert failed: ({}) | file={} | line={} | func={}", expr, file, line, func);
	}

	template <typename... Args>
	inline void OnAssertFailureMsg(
		const char* expr,
		const char* file,
		int line,
		const char* func,
		std::string_view format,
		Args&&... args) noexcept
	{
		const std::string message = FormatLogMessage(format, std::forward<Args>(args)...);
		JAM_LOG_FATAL("Assert failed: ({}) | msg={} | file={} | line={} | func={}", expr, message, file, line, func);
	}

	inline void OnCheckFailure(const char* expr, const char* file, int line, const char* func) noexcept
	{
		JAM_LOG_ERROR("Check failed: ({}) | file={} | line={} | func={}", expr, file, line, func);
	}

	template <typename... Args>
	inline void OnCheckFailureMsg(
		const char* expr,
		const char* file,
		int line,
		const char* func,
		std::string_view format,
		Args&&... args) noexcept
	{
		const std::string message = FormatLogMessage(format, std::forward<Args>(args)...);
		JAM_LOG_ERROR("Check failed: ({}) | msg={} | file={} | line={} | func={}", expr, message, file, line, func);
	}
}

#if JAM_DEBUG

#define JAM_ASSERT(expr)                                                            \
	do                                                                              \
	{                                                                               \
		if (!(expr))                                                                \
		{                                                                           \
			::jam::detail::OnAssertFailure(#expr, __FILE__, __LINE__, __func__);    \
			JAM_DEBUG_BREAK();                                                      \
		}                                                                           \
	} while (false)

#define JAM_ASSERT_MSG(expr, ...)                                                   \
	do                                                                              \
	{                                                                               \
		if (!(expr))                                                                \
		{                                                                           \
			::jam::detail::OnAssertFailureMsg(#expr, __FILE__, __LINE__, __func__,  \
				__VA_ARGS__);                                                       \
			JAM_DEBUG_BREAK();                                                      \
		}                                                                           \
	} while (false)

#else

#define JAM_ASSERT(expr) ((void)0)
#define JAM_ASSERT_MSG(expr, ...) ((void)0)

#endif

#if JAM_DEBUG

#define JAM_VERIFY(expr)                                                            \
	do                                                                              \
	{                                                                               \
		const bool _jam_verify_result = !!(expr);                                   \
		if (!_jam_verify_result)                                                    \
		{                                                                           \
			::jam::detail::OnAssertFailure(#expr, __FILE__, __LINE__, __func__);    \
			JAM_DEBUG_BREAK();                                                      \
		}                                                                           \
	} while (false)

#define JAM_VERIFY_MSG(expr, ...)                                                   \
	do                                                                              \
	{                                                                               \
		const bool _jam_verify_result = !!(expr);                                   \
		if (!_jam_verify_result)                                                    \
		{                                                                           \
			::jam::detail::OnAssertFailureMsg(#expr, __FILE__, __LINE__, __func__,  \
				__VA_ARGS__);                                                       \
			JAM_DEBUG_BREAK();                                                      \
		}                                                                           \
	} while (false)

#else

#define JAM_VERIFY(expr) ((void)(expr))
#define JAM_VERIFY_MSG(expr, ...) ((void)(expr))

#endif

#define JAM_CRASH(...)                                                              \
	do                                                                              \
	{                                                                               \
		::jam::detail::OnAssertFailureMsg("JAM_CRASH", __FILE__, __LINE__, __func__,\
			__VA_ARGS__);                                                           \
		JAM_DEBUG_BREAK();                                                          \
		std::abort();                                                               \
	} while (false)

#define JAM_CHECK(expr, action)                                                     \
	do                                                                              \
	{                                                                               \
		if (!(expr))                                                                \
		{                                                                           \
			::jam::detail::OnCheckFailure(#expr, __FILE__, __LINE__, __func__);     \
			action;                                                                 \
		}                                                                           \
	} while (false)

#define JAM_CHECK_MSG(expr, action, ...)                                             \
	do                                                                              \
	{                                                                               \
		if (!(expr))                                                                \
		{                                                                           \
			::jam::detail::OnCheckFailureMsg(#expr, __FILE__, __LINE__, __func__,   \
				__VA_ARGS__);                                                       \
			action;                                                                 \
		}                                                                           \
	} while (false)

#define JAM_ASSERT_OR_RETURN(expr)                                                  \
	do                                                                              \
	{                                                                               \
		JAM_ASSERT(expr);                                                           \
		if (!(expr))                                                                \
		{                                                                           \
			::jam::detail::OnCheckFailure(#expr, __FILE__, __LINE__, __func__);     \
			return;                                                                 \
		}                                                                           \
	} while (false)

#define JAM_ASSERT_OR_RETURN_MSG(expr, ...)                                         \
	do                                                                              \
	{                                                                               \
		JAM_ASSERT_MSG(expr, __VA_ARGS__);                                           \
		if (!(expr))                                                                \
		{                                                                           \
			::jam::detail::OnCheckFailureMsg(#expr, __FILE__, __LINE__, __func__,   \
				__VA_ARGS__);                                                       \
			return;                                                                 \
		}                                                                           \
	} while (false)

#define JAM_ASSERT_OR_RETURN_VALUE(expr, value)                                     \
	do                                                                              \
	{                                                                               \
		JAM_ASSERT(expr);                                                           \
		if (!(expr))                                                                \
		{                                                                           \
			::jam::detail::OnCheckFailure(#expr, __FILE__, __LINE__, __func__);     \
			return (value);                                                         \
		}                                                                           \
	} while (false)

#define JAM_ASSERT_OR_RETURN_VALUE_MSG(expr, value, ...)                            \
	do                                                                              \
	{                                                                               \
		JAM_ASSERT_MSG(expr, __VA_ARGS__);                                           \
		if (!(expr))                                                                \
		{                                                                           \
			::jam::detail::OnCheckFailureMsg(#expr, __FILE__, __LINE__, __func__,   \
				__VA_ARGS__);                                                       \
			return (value);                                                         \
		}                                                                           \
	} while (false)

#if defined(_MSC_VER)
#define JAM_UNREACHABLE() __assume(0)
#elif defined(__clang__) || defined(__GNUC__)
#define JAM_UNREACHABLE() __builtin_unreachable()
#else
#define JAM_UNREACHABLE() ((void)0)
#endif

#define JAM_ASSERT_UNREACHABLE()                                                    \
	do                                                                              \
	{                                                                               \
		JAM_ASSERT_MSG(false, "Unreachable code path");                             \
		JAM_UNREACHABLE();                                                          \
	} while (false)
