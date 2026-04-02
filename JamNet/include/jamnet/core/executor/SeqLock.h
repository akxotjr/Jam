#pragma once

#include <jambase/CacheLine.h>

namespace jam::net
{
    // Writer 단일 가정 seqlock.
    // (멀티 writer이면 외부에서 writer mutex를 걸거나 아래 SeqLockBoxMT를 사용)
    class SeqLock
    {
    public:
        SeqLock() = default;

        void WriteBegin() noexcept
        {
            // release: 이후의 data write들이 reader acquire로 관측되도록 anchor 역할
            m_seq.value.fetch_add(1, std::memory_order_release);
        }

        void WriteEnd() noexcept
        {
            m_seq.value.fetch_add(1, std::memory_order_release);
        }

        std::uint64_t ReadBegin() const noexcept
        {
            return m_seq.value.load(std::memory_order_acquire);
        }

        bool ReadRetry(std::uint64_t begin) const noexcept
        {
            const auto end = m_seq.value.load(std::memory_order_acquire);
            return (begin & 1u) || (begin != end);
        }

    private:
        CachelineIsolated<std::atomic<std::uint64_t>> m_seq{ 0 };
    };

    // ============================================================
    // SeqLockBox<T>: seqlock으로 보호되는 값 한 덩어리
    // ============================================================

    template<class T>
    class SeqLockBox
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "SeqLockBox<T>는 trivially copyable 데이터에만 권장됩니다. "
            "포인터 따라가는 구조/컨테이너/shared_ptr 등에는 부적합합니다.");

    public:
        SeqLockBox() = default;
        explicit SeqLockBox(const T& init) : m_data(init) {}

        // Writer: 단일 writer 가정
        void Write(const T& v) noexcept
        {
            m_lock.WriteBegin();
            m_data = v;
            m_lock.WriteEnd();
        }

        // Reader: 한 번 시도해서 성공하면 true
        bool TryRead(T& out) const noexcept
        {
            const auto s = m_lock.ReadBegin();
            if (s & 1u) return false;

            out = m_data;

            return !m_lock.ReadRetry(s);
        }

        // Reader: bounded retry 스핀 (성공하면 true, 실패하면 false)
        bool ReadSpin(T& out, std::uint32_t maxTries = 1000) const noexcept
        {
            for (std::uint32_t i = 0; i < maxTries; ++i)
            {
                if (TryRead(out)) return true;


#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
                _mm_pause();
#elif defined(__i386__) || defined(__x86_64__)
                __builtin_ia32_pause();
#endif
            }
            return false;
        }


    private:
        SeqLock     m_lock;         // seq는 단독 캐시라인
        T           m_data{};     // 그 다음 캐시라인(들)
    };
}
