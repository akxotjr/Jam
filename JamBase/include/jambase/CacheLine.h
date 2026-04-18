#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>        // std::hardware_destructive_interference_size
#include <type_traits>
#include <utility>    // std::forward

#ifndef JAM_CACHELINE_FALLBACK
// 대부분의 x86/x64는 64B. (ARM도 보통 64)
#define JAM_CACHELINE_FALLBACK 64
#endif

namespace jam
{
    // "같은 캐시라인 공유"는 성능을 깨뜨리는 대표 원인(false sharing).
    // destructive_interference_size는 표준이지만 모든 STL이 지원하진 않을 수 있어 fallback 제공.
    constexpr std::size_t destructive_interference_size =
#ifdef __cpp_lib_hardware_interference_size
	    std::hardware_destructive_interference_size;
#else
        JAM_CACHELINE_FALLBACK;
#endif

    template<std::size_t Align>
    struct alignas(Align) AlignedStorage
    {
        std::byte _pad[Align];
    };

    // N 바이트를 Align 경계까지 올림
    constexpr std::size_t AlignUp(std::size_t n, std::size_t align) noexcept
    {
        return (n + align - 1) / align * align;
    }

    constexpr std::size_t AlignDown(std::size_t n, std::size_t align) noexcept
    {
        return n / align * align;
    }

    // "멤버 하나를 캐시라인 단독 점유"시키는 래퍼
    template<class T, std::size_t Align = destructive_interference_size>
    struct alignas(Align) CachelineIsolated
    {
        static_assert(Align >= alignof(T));
        T value;

        // 나머지 공간을 패딩으로 채워서 sizeof(CachelineIsolated<T>) == Align (또는 AlignUp)
        static constexpr std::size_t kSize = AlignUp(sizeof(T), Align);
        std::byte padding[kSize - sizeof(T)]{};

        CachelineIsolated() = default;

        template<class... Args>
        explicit CachelineIsolated(Args&&... args) : value(std::forward<Args>(args)...)
        {
        }
    };

    static_assert(CachelineIsolated<std::atomic<std::uint64_t>>::kSize == destructive_interference_size);
}