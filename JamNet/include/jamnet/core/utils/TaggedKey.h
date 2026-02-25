#pragma once
#include <string_view>

namespace jam
{
    // 64-bit FNV-1a
    constexpr uint64 fnv1a64(std::string_view s)
    {
        uint64 h = 1469598103934665603ull;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        return h;
    }

    template<class Tag>
    struct Key
    {
        uint64 v{};
        constexpr auto operator<=>(const Key&) const = default;
    };

    template<class KeyT>
    constexpr KeyT KeyOf(std::string_view name)
    {
        return KeyT{ fnv1a64(name) };
    }
}