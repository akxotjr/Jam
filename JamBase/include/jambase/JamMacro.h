#pragma once

#define OUT
#define INOUT

#define size16(val)		static_cast<int16>(sizeof(val))
#define size32(val)		static_cast<int32>(sizeof(val))
#define len16(arr)		static_cast<int16>(sizeof(arr) / sizeof(arr[0]))
#define len32(arr)		static_cast<int32>(sizeof(arr) / sizeof(arr[0]))

#define DECLARE_SINGLETON(ClassType)                            \
public:                                                         \
    static ClassType& Instance()                                \
    {                                                           \
        static ClassType instance;                              \
        return instance;                                        \
    }                                                           \
private:                                                        \
    ClassType() = default;                                      \
    ~ClassType() = default;                                     \
    ClassType(const ClassType&) = delete;                       \
    ClassType& operator=(const ClassType&) = delete;

#define DECLARE_SINGLETON_INHERITANCE(ClassType)                \
public:                                                         \
    static ClassType& Instance()                                \
    {                                                           \
        static ClassType instance;                              \
        return instance;                                        \
    }                                                           \
private:                                                        \
    ClassType() = default;                                      \
    ~ClassType() override = default;                            \
    ClassType(const ClassType&) = delete;                       \
    ClassType& operator=(const ClassType&) = delete;


#define E2U(e) static_cast<std::underlying_type_t<decltype(e)>>(e)
#define U2E(EnumType, v) static_cast<EnumType>(v)



#define JAM_FORCE_INLINE __forceinline


// ============================================================
//  Compiler detection
// ============================================================

#if defined(__clang__)
#define JAM_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define JAM_COMPILER_GCC 1
#elif defined(_MSC_VER)
#define JAM_COMPILER_MSVC 1
#endif


// ============================================================
//  Likely / Unlikely
// ============================================================

#if defined(__has_cpp_attribute)

#if __has_cpp_attribute(likely)
#define JAM_LIKELY [[likely]]
#else
#define JAM_LIKELY
#endif

#if __has_cpp_attribute(unlikely)
#define JAM_UNLIKELY [[unlikely]]
#else
#define JAM_UNLIKELY
#endif

#else
#define JAM_LIKELY
#define JAM_UNLIKELY
#endif


// ============================================================
//  Fallthrough
// ============================================================

#if defined(__has_cpp_attribute)

#if __has_cpp_attribute(fallthrough)
#define JAM_FALLTHROUGH [[fallthrough]]
#else
#define JAM_FALLTHROUGH
#endif

#else
#define JAM_FALLTHROUGH
#endif


// ============================================================
//  Nodiscard
// ============================================================

#if defined(__has_cpp_attribute)

#if __has_cpp_attribute(nodiscard)
#define JAM_NODISCARD [[nodiscard]]
#else
#define JAM_NODISCARD
#endif

#else
#define JAM_NODISCARD
#endif


// ============================================================
//  Maybe unused
// ============================================================

#if defined(__has_cpp_attribute)

#if __has_cpp_attribute(maybe_unused)
#define JAM_MAYBE_UNUSED [[maybe_unused]]
#else
#define JAM_MAYBE_UNUSED
#endif

#else
#define JAM_MAYBE_UNUSED
#endif


// ============================================================
//  Deprecated
// ============================================================

#if defined(__has_cpp_attribute)

#if __has_cpp_attribute(deprecated)
#define JAM_DEPRECATED(msg) [[deprecated(msg)]]
#else
#define JAM_DEPRECATED(msg)
#endif

#else
#define JAM_DEPRECATED(msg)
#endif


// ============================================================
//  no_unique_address
// ============================================================

#if defined(__has_cpp_attribute)

#if __has_cpp_attribute(no_unique_address)
#define JAM_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define JAM_NO_UNIQUE_ADDRESS
#endif

#else
#define JAM_NO_UNIQUE_ADDRESS
#endif
