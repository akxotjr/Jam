#pragma once

#include <type_traits>
#include <cstdint>

namespace jam
{
    template<class E>
    using underlying_t = std::underlying_type_t<E>;

    template<class E>
    constexpr underlying_t<E> u(E e) noexcept { return static_cast<underlying_t<E>>(e); }

    template<class Enum, class Storage = underlying_t<Enum>>
    class FlagsT
    {
        static_assert(std::is_enum_v<Enum>, "FlagsT<Enum>: Enum must be an enum");
        static_assert(std::is_integral_v<Storage>, "FlagsT: Storage must be integral");

    public:
        using enum_type = Enum;
        using storage_type = Storage;

        constexpr FlagsT() noexcept = default;
        constexpr FlagsT(Enum e) noexcept : m_bits(static_cast<Storage>(u(e))) {}
        constexpr FlagsT(Storage bits) noexcept : m_bits(bits) {}

        static constexpr FlagsT None() noexcept { return FlagsT(Storage{ 0 }); }

        constexpr Storage bits() const noexcept { return m_bits; }
        constexpr explicit operator bool() const noexcept { return m_bits != 0; }

        // queries
        constexpr bool any() const noexcept { return m_bits != 0; }
        constexpr bool has_any(FlagsT mask) const noexcept { return (m_bits & mask.m_bits) != 0; }
        constexpr bool has_all(FlagsT mask) const noexcept { return (m_bits & mask.m_bits) == mask.m_bits; }

        // modifiers
        constexpr void set(Enum e)    noexcept { m_bits |= static_cast<Storage>(u(e)); }
        constexpr void reset(Enum e)  noexcept { m_bits &= ~static_cast<Storage>(u(e)); }
        constexpr void toggle(Enum e) noexcept { m_bits ^= static_cast<Storage>(u(e)); }
        constexpr void clear()        noexcept { m_bits = 0; }

        // ops (Flags <-> Flags)
        friend constexpr FlagsT operator|(FlagsT a, FlagsT b) noexcept { return FlagsT(a.m_bits | b.m_bits); }
        friend constexpr FlagsT operator&(FlagsT a, FlagsT b) noexcept { return FlagsT(a.m_bits & b.m_bits); }
        friend constexpr FlagsT operator^(FlagsT a, FlagsT b) noexcept { return FlagsT(a.m_bits ^ b.m_bits); }
        friend constexpr FlagsT operator~(FlagsT a) noexcept { return FlagsT(static_cast<Storage>(~a.m_bits)); }

        friend constexpr FlagsT& operator|=(FlagsT& a, FlagsT b) noexcept { a.m_bits |= b.m_bits; return a; }
        friend constexpr FlagsT& operator&=(FlagsT& a, FlagsT b) noexcept { a.m_bits &= b.m_bits; return a; }
        friend constexpr FlagsT& operator^=(FlagsT& a, FlagsT b) noexcept { a.m_bits ^= b.m_bits; return a; }

        // ops (Enum <-> Enum/Flags) 
        friend constexpr FlagsT operator|(Enum a, Enum b) noexcept { return FlagsT(a) | FlagsT(b); }
        friend constexpr FlagsT operator&(Enum a, Enum b) noexcept { return FlagsT(a) & FlagsT(b); }
        friend constexpr FlagsT operator^(Enum a, Enum b) noexcept { return FlagsT(a) ^ FlagsT(b); }

        friend constexpr FlagsT operator|(FlagsT a, Enum b) noexcept { return a | FlagsT(b); }
        friend constexpr FlagsT operator&(FlagsT a, Enum b) noexcept { return a & FlagsT(b); }
        friend constexpr FlagsT operator^(FlagsT a, Enum b) noexcept { return a ^ FlagsT(b); }

        friend constexpr FlagsT operator|(Enum a, FlagsT b) noexcept { return FlagsT(a) | b; }
        friend constexpr FlagsT operator&(Enum a, FlagsT b) noexcept { return FlagsT(a) & b; }
        friend constexpr FlagsT operator^(Enum a, FlagsT b) noexcept { return FlagsT(a) ^ b; }

    private:
        Storage m_bits{ 0 };
    };


} // namespace jam
