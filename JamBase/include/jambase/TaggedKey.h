#pragma once

#include <bit>
#include <compare>
#include <cstddef>
#include <functional>
#include <limits>
#include <string_view>
#include <type_traits>

#include <xxhash.h>

#include "jambase/JamTypes.h"

namespace jam
{
	template<class Tag, class UInt = uint64>
	struct TagKey
	{
		static_assert(std::is_unsigned_v<UInt>, "TagKey storage must be unsigned");

		using tag_type   = Tag;
		using value_type = UInt;

		UInt v = 0;

		constexpr TagKey() = default;
		constexpr explicit TagKey(UInt value) : v(value) {}

		constexpr UInt value() const noexcept { return v; }
		constexpr explicit operator bool() const noexcept { return v != 0; }
		constexpr auto operator<=>(const TagKey&) const = default;
	};

	namespace detail
	{
		[[nodiscard]] inline constexpr uint16 ToLittleEndian16(uint16 x) noexcept
		{
			if constexpr (std::endian::native == std::endian::little)
				return x;
			
			return static_cast<uint16>((x >> 8) | (x << 8));
		}

		[[nodiscard]] inline constexpr uint32 ToLittleEndian32(uint32 x) noexcept
		{
			if constexpr (std::endian::native == std::endian::little)
				return x;

			return (x >> 24) |
				  ((x >> 8) & 0x0000FF00u) |
				  ((x << 8) & 0x00FF0000u) |
				   (x << 24);
		}

		[[nodiscard]] inline constexpr uint64 ToLittleEndian64(uint64 x) noexcept
		{
			if constexpr (std::endian::native == std::endian::little)
				return x;
			return (x >> 56) |
				  ((x >> 40) & 0x000000000000FF00ull) |
				  ((x >> 24) & 0x0000000000FF0000ull) |
				  ((x >> 8)  & 0x00000000FF000000ull) |
				  ((x << 8)  & 0x000000FF00000000ull) |
				  ((x << 24) & 0x0000FF0000000000ull) |
				  ((x << 40) & 0x00FF000000000000ull) |
				   (x << 56);
		}
	}

	[[nodiscard]] inline uint64 HashTagKey(std::string_view name, uint64 seed = 0) noexcept
	{
		const void* data = name.empty() ? nullptr : name.data();
		return static_cast<uint64>(XXH3_64bits_withSeed(data, name.size(), seed));
	}

	[[nodiscard]] inline uint64 HashTagKey(uint64 value, uint64 seed = 0) noexcept
	{
		const uint64 le = detail::ToLittleEndian64(value);
		return static_cast<uint64>(XXH3_64bits_withSeed(&le, sizeof(le), seed));
	}

	[[nodiscard]] inline uint64 HashTagKey(uint32 value, uint64 seed = 0) noexcept
	{
		const uint32 le = detail::ToLittleEndian32(value);
		return static_cast<uint64>(XXH3_64bits_withSeed(&le, sizeof(le), seed));
	}

	[[nodiscard]] inline uint64 HashTagKey(uint16 value, uint64 seed = 0) noexcept
	{
		const uint16 le = detail::ToLittleEndian16(value);
		return static_cast<uint64>(XXH3_64bits_withSeed(&le, sizeof(le), seed));
	}

	[[nodiscard]] inline uint64 HashTagKey(std::string_view name, uint64 value, uint64 seed) noexcept
	{
		uint64 h = HashTagKey(name, seed);
		return HashTagKey(value, h);
	}

	template<class KeyT>
	[[nodiscard]] inline KeyT TagKeyOf(std::string_view name, uint64 seed = 0) noexcept
	{
		return KeyT{ static_cast<typename KeyT::value_type>(HashTagKey(name, seed)) };
	}

	template<class KeyT>
	[[nodiscard]] inline KeyT TagKeyOf(uint64 value, uint64 seed = 0) noexcept
	{
		return KeyT{ static_cast<typename KeyT::value_type>(HashTagKey(value, seed)) };
	}

	template<class KeyT>
	[[nodiscard]] inline bool IsValidTagKey(KeyT key) noexcept
	{
		return key.value() != 0;
	}
}

namespace std
{
	template<class Tag, class UInt>
	struct hash<jam::TagKey<Tag, UInt>>
	{
		size_t operator()(jam::TagKey<Tag, UInt> key) const noexcept
		{
			if constexpr (sizeof(size_t) >= sizeof(UInt))
				return static_cast<size_t>(key.value());
			else
				return static_cast<size_t>(key.value() & static_cast<UInt>(std::numeric_limits<size_t>::max()));
		}
	};
}
