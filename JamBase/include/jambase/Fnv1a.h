#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <string>
#include <type_traits>
#include <bit>        // std::endian (C++20)
#include <cstring>    // std::memcpy



namespace jam
{
    // =========================================================
    //  FNV-1a traits
    // =========================================================
    template<class UInt>
    struct Fnv1aTraits;

    template<>
    struct Fnv1aTraits<uint32_t>
    {
        static constexpr uint32_t offset_basis  = 2166136261u;
        static constexpr uint32_t prime         = 16777619u;
    };

    template<>
    struct Fnv1aTraits<uint64_t>
    {
        static constexpr uint64_t offset_basis  = 14695981039346656037ull;
        static constexpr uint64_t prime         = 1099511628211ull;
    };

    template<class UInt>
    concept FnvUInt = std::is_unsigned_v<UInt> && (sizeof(UInt) == 4 || sizeof(UInt) == 8);

    // =========================================================
    //  Byte helpers
    // =========================================================
    [[nodiscard]] inline constexpr uint8_t u8(unsigned char c) noexcept
    {
        return static_cast<uint8_t>(c);
    }

    template<class T>
    concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

    // =========================================================
    //  Endian-stable append helpers (AppendLE / AppendBytes)
    //
    //  - "같은 값"이면 어떤 CPU endian에서도 동일한 바이트열로 해싱
    //  - float/double도 bit-pattern 기준으로 LE로 고정
    // =========================================================
    template<FnvUInt UInt>
    [[nodiscard]] inline UInt fnv1a_append_bytes(UInt h, const void* data, size_t len) noexcept
    {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < len; ++i)
        {
            h ^= static_cast<UInt>(u8(p[i]));
            h *= static_cast<UInt>(Fnv1aTraits<UInt>::prime);
        }
        return h;
    }

    template<FnvUInt UInt>
    [[nodiscard]] constexpr UInt fnv1a_init() noexcept
    {
        return static_cast<UInt>(Fnv1aTraits<UInt>::offset_basis);
    }

    // byteswap (C++20: std::byteswap은 C++23)
    [[nodiscard]] inline constexpr uint16_t bswap16(uint16_t x) noexcept
    {
        return static_cast<uint16_t>((x >> 8) | (x << 8));
    }
    [[nodiscard]] inline constexpr uint32_t bswap32(uint32_t x) noexcept
    {
        return (x >> 24) |
              ((x >> 8) & 0x0000FF00u) |
              ((x << 8) & 0x00FF0000u) |
               (x << 24);
    }
    [[nodiscard]] inline constexpr uint64_t bswap64(uint64_t x) noexcept
    {
        return (x >> 56) |
              ((x >> 40) & 0x000000000000FF00ull) |
              ((x >> 24) & 0x0000000000FF0000ull) |
              ((x >> 8)  & 0x00000000FF000000ull) |
              ((x << 8)  & 0x000000FF00000000ull) |
              ((x << 24) & 0x0000FF0000000000ull) |
              ((x << 40) & 0x00FF000000000000ull) |
               (x << 56);
    }



    template<class T>
    struct endian_base
    {
        using type = T;
    };

    template<class T> requires std::is_enum_v<T>
    struct endian_base<T>
    {
        using type = std::underlying_type_t<T>;
    };

    template<class T>
    using endian_base_t = typename endian_base<T>::type;


    template<class T>
    [[nodiscard]] inline constexpr T to_little_endian(T v) noexcept
    {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T>, "to_little_endian expects integral/enum");

        using Base = endian_base_t<T>; // enum이면 underlying, 아니면 그대로 T
        static_assert(std::is_integral_v<Base> && !std::is_same_v<Base, bool>, "to_little_endian: bool is not supported");

        using U = std::make_unsigned_t<Base>;

        U x = static_cast<U>(static_cast<Base>(v));

        if constexpr (sizeof(U) == 1)
        {
            return static_cast<T>(static_cast<Base>(x));
        }
        else if constexpr (sizeof(U) == 2)
        {
            if constexpr (std::endian::native == std::endian::little)
                return static_cast<T>(static_cast<Base>(x));
            else
                return static_cast<T>(static_cast<Base>(bswap16(static_cast<uint16_t>(x))));
        }
        else if constexpr (sizeof(U) == 4)
        {
            if constexpr (std::endian::native == std::endian::little)
                return static_cast<T>(static_cast<Base>(x));
            else
                return static_cast<T>(static_cast<Base>(bswap32(static_cast<uint32_t>(x))));
        }
        else if constexpr (sizeof(U) == 8)
        {
            if constexpr (std::endian::native == std::endian::little)
                return static_cast<T>(static_cast<Base>(x));
            else
                return static_cast<T>(static_cast<Base>(bswap64(static_cast<uint64_t>(x))));
        }
        else
        {
            static_assert(sizeof(U) <= 8, "unsupported size");
            return v;
        }
    }



    // =========================================================
    //  Streaming hasher
    // =========================================================
    template<FnvUInt UInt>
    struct Fnv1a
    {
        using value_type = UInt;
        UInt h = fnv1a_init<UInt>();

        constexpr Fnv1a() noexcept = default;
        constexpr explicit Fnv1a(UInt seed) noexcept : h(seed) {}

        inline void AppendBytes(const void* p, size_t n) noexcept
        {
            h = fnv1a_append_bytes<UInt>(h, p, n);
        }

        // endian-stable for integral/enum types
        template<class T> requires (std::is_integral_v<T> || std::is_enum_v<T>)
        inline void AppendLE(T v) noexcept
        {
            T le = to_little_endian(v);
            AppendBytes(&le, sizeof(T));
        }

        // float/double: bit-pattern을 LE로 고정
        template<class F> requires (std::is_floating_point_v<F>)
        inline void AppendLE(F v) noexcept
        {
            using I = std::conditional_t<sizeof(F) == 4, uint32_t, uint64_t>;
            static_assert(sizeof(F) == 4 || sizeof(F) == 8, "only float/double supported");

            I bits{};
            std::memcpy(&bits, &v, sizeof(F));
            bits = to_little_endian(bits);
            AppendBytes(&bits, sizeof(bits));
        }

        [[nodiscard]] constexpr UInt Value() const noexcept { return h; }
        constexpr void Reset() noexcept { h = fnv1a_init<UInt>(); }
        constexpr void Reset(UInt seed) noexcept { h = seed; }
    };

    using Fnv1a32 = Fnv1a<uint32_t>;
    using Fnv1a64 = Fnv1a<uint64_t>;

    // =========================================================
    //  One-shot constexpr string_view (문자열만)
    // =========================================================
    template<FnvUInt UInt>
    [[nodiscard]] constexpr UInt fnv1a(std::string_view s) noexcept
    {
        UInt h = fnv1a_init<UInt>();
        for (unsigned char c : s)
        {
            h ^= static_cast<UInt>(u8(c));
            h *= static_cast<UInt>(Fnv1aTraits<UInt>::prime);
        }
        return h;
    }

    // =========================================================
    //  ADL extension point
    //
    //  - 아래 HashAppend 오버로드들은 "기본 제공"
    //  - 사용자 타입은 "자기 네임스페이스"에:
    //      void HashAppend(jam::Fnv1a64& h, const MyType& v);
    //    를 정의하면 됨 (ADL로 자동 탐색)
    //
    //  - jam 내부에서는 반드시 `HashAppend(h, x)` 형태로 호출(앞에 jam:: 붙이지 않음)
    // =========================================================

    // --- primitives
    template<FnvUInt UInt>
    inline void HashAppend(Fnv1a<UInt>& h, bool v) noexcept
    {
        h.template AppendLE<uint8_t>(v ? 1u : 0u);
    }

    template<FnvUInt UInt, class T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
    inline void HashAppend(Fnv1a<UInt>& h, T v) noexcept
    {
        // 값 기반(LE 고정)
        h.AppendLE(v);
    }

    template<FnvUInt UInt, class E> requires std::is_enum_v<E>
    inline void HashAppend(Fnv1a<UInt>& h, E e) noexcept
    {
        using U = std::underlying_type_t<E>;
        if constexpr (std::is_signed_v<U>)
            h.AppendLE(static_cast<std::make_unsigned_t<U>>(static_cast<U>(e)));
        else
            h.AppendLE(static_cast<U>(e));
    }

    template<FnvUInt UInt, class F> requires std::is_floating_point_v<F>
    inline void HashAppend(Fnv1a<UInt>& h, F v) noexcept
    {
        h.AppendLE(v);
    }

    // --- bytes (raw): boundary ambiguity가 생길 수 있으니 보통은 길이 포함해서 쓰는걸 권장
    template<FnvUInt UInt>
    inline void HashAppendBytes(Fnv1a<UInt>& h, const void* data, size_t n) noexcept
    {
        h.AppendBytes(data, n);
    }

    // --- string: length prefix 포함(경계 모호성 제거)
    template<FnvUInt UInt>
    inline void HashAppend(Fnv1a<UInt>& h, std::string_view s) noexcept
    {
        if constexpr (sizeof(UInt) == 4) h.template AppendLE<uint32_t>(static_cast<uint32_t>(s.size()));
        else                             h.template AppendLE<uint64_t>(static_cast<uint64_t>(s.size()));

        if (!s.empty())
            h.AppendBytes(s.data(), s.size());
    }

    template<FnvUInt UInt>
    inline void HashAppend(Fnv1a<UInt>& h, const std::string& s) noexcept
    {
        HashAppend(h, std::string_view{ s });
    }

    // --- POD fallback (주의: 엔디안/패딩/부동소수/포인터 포함 타입이면 결과가 흔들릴 수 있음)
    //     그래서 "명시적으로 허용"한 경우만 쓰도록 막아두는게 안전함.
    template<class T>
    struct allow_pod_hash : std::false_type {};

    template<class T>
    inline constexpr bool allow_pod_hash_v = allow_pod_hash<T>::value;

    template<FnvUInt UInt, class T> requires (TriviallyCopyable<T>&& allow_pod_hash_v<T>)
    inline void HashAppend(Fnv1a<UInt>& h, const T& v) noexcept
    {
        // 이건 바이트 그대로(endianness/패딩이 포함될 수 있음)
        h.AppendBytes(&v, sizeof(T));
    }

    // =========================================================
    //  HashOf: 여러 요소를 순서대로 해싱
    //
    //  핵심: 여기서 `HashAppend(h, x)`를 "jam::" 없이 호출해야 ADL이 작동함.
    // =========================================================
    template<FnvUInt UInt, class... Ts>
    [[nodiscard]] inline UInt HashOf(const Ts&... xs) noexcept
    {
        Fnv1a<UInt> h;
        (HashAppend(h, xs), ...); // <-- ADL 확장 포인트
        return h.Value();
    }

    template<FnvUInt UInt, class... Ts>
    [[nodiscard]] inline UInt HashOfSeed(UInt seed, const Ts&... xs) noexcept
    {
        Fnv1a<UInt> h(seed);
        (HashAppend(h, xs), ...);
        return h.Value();
    }


    namespace detail
    {
        template<class H, class T, class... Ms>
        inline void HashFields(H& h, const T& obj, Ms T::*... members) noexcept
        {
            using jam::HashAppend;
            (HashAppend(h, obj.*members), ...);
        }
    }   // namespace jam::detail


    template<class Tag, FnvUInt UInt>
    struct Fnv1aHandle
    {
        using value_type = UInt;

        UInt v{ 0 };

        constexpr auto operator<=>(const Fnv1aHandle&) const = default;
        constexpr explicit operator bool() const noexcept { return v != 0; }

        template<class T>
        static consteval Fnv1aHandle FromType()
        {
            return Fnv1aHandle{ fnv1a<T>() };
        }

        static constexpr Fnv1aHandle FromU64(uint64_t h) noexcept
        {
            return Fnv1aHandle{ static_cast<UInt>(h) };
        }

        static constexpr Fnv1aHandle FromU32(uint32 h) noexcept
        {
            return Fnv1aHandle{ static_cast<UInt>(h) };
        }
    };

    template<class Handle, class Def>
    inline Handle Fnv1aHandleOf(const Def& def) noexcept
    {
        using U = typename Handle::value_type; // or Handle::storage_type 같은 alias 만들어도 됨
        if constexpr (sizeof(U) == 4)
            return Handle::FromU32(jam::HashOf<uint32_t>(def));
        else
            return Handle::FromU64(jam::HashOf<uint64_t>(def));
    }




} // namespace jam



namespace std
{
    template<class Tag, class UInt>
    struct hash<jam::Fnv1aHandle<Tag, UInt>>
    {
        size_t operator()(const jam::Fnv1aHandle<Tag, UInt>& h) const noexcept
        {
            // size_t may be 64-bit or 32-bit; convert the handle value appropriately.
            if constexpr (sizeof(size_t) >= sizeof(UInt))
            {
                return static_cast<size_t>(h.v);
            }
            else
            {
                // truncate if size_t smaller than UInt
                return static_cast<size_t>(h.v & static_cast<UInt>(std::numeric_limits<size_t>::max()));
            }
        }
    };
}


#define JAM_FNV1A32_HASHABLE(Type, ...)                                  \
    inline void HashAppend(jam::Fnv1a32& h, const Type& v) noexcept      \
    {                                                                    \
        jam::detail::HashFields(h, v, __VA_ARGS__);                      \
    }

#define JAM_FNV1A64_HASHABLE(Type, ...)                                  \
    inline void HashAppend(jam::Fnv1a64& h, const Type& v) noexcept      \
    {                                                                    \
        jam::detail::HashFields(h, v, __VA_ARGS__);                      \
    }


