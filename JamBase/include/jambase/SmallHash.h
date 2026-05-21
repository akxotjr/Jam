#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <bit>

namespace jam
{
	namespace detail
	{
		inline constexpr uint64_t kGoldenRatio64 = 0x9e3779b97f4a7c15ull;

		[[nodiscard]]
		inline constexpr uint64_t Mix64(uint64_t x) noexcept
		{
			// splitmix64 finalizer
			x ^= x >> 30;
			x *= 0xbf58476d1ce4e5b9ull;
			x ^= x >> 27;
			x *= 0x94d049bb133111ebull;
			x ^= x >> 31;
			return x;
		}

		[[nodiscard]]
		inline constexpr uint32_t Mix32(uint32_t x) noexcept
		{
			// MurmurHash3 fmix32 style
			x ^= x >> 16;
			x *= 0x85ebca6bu;
			x ^= x >> 13;
			x *= 0xc2b2ae35u;
			x ^= x >> 16;
			return x;
		}

		template<class T>
		[[nodiscard]]
		inline constexpr uint64_t To64(T v) noexcept
		{
			static_assert(std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>);

			if constexpr (std::is_enum_v<T>)
			{
				using U = std::underlying_type_t<T>;
				return static_cast<uint64_t>(static_cast<std::make_unsigned_t<U>>(static_cast<U>(v)));
			}
			else if constexpr (std::is_pointer_v<T>)
			{
				return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(v));
			}
			else
			{
				using U = std::make_unsigned_t<T>;
				return static_cast<uint64_t>(static_cast<U>(v));
			}
		}

	} // namespace jam::detail

	struct SmallHash64
	{
		uint64_t h = detail::kGoldenRatio64;

		constexpr SmallHash64() noexcept = default;

		explicit  constexpr SmallHash64(uint64_t seed) noexcept
			: h(seed + detail::kGoldenRatio64)
		{
		}

		template<class T>
		constexpr void Append(T v) noexcept
		{
			uint64_t x = detail::To64(v);
			x = detail::Mix64(x);

			h ^= x + detail::kGoldenRatio64 + (h << 6) + (h >> 2);
		}

		[[nodiscard]]
		constexpr uint64_t Value64() const noexcept
		{
			return detail::Mix64(h);
		}

		[[nodiscard]]
		constexpr size_t Value() const noexcept
		{
			if constexpr (sizeof(size_t) >= 8)
			{
				return static_cast<size_t>(Value64());
			}
			else
			{
				return static_cast<size_t>(detail::Mix32(static_cast<uint32_t>(Value64())));
			}
		}
	};

	template<class... Ts>
	[[nodiscard]]
	constexpr size_t SmallHashOf(const Ts&... xs) noexcept
	{
		SmallHash64 h;
		(h.Append(xs), ...);
		return h.Value();
	}

	template<class... Ts>
	[[nodiscard]]
	constexpr size_t SmallHashOfSeed(uint64_t seed, const Ts&... xs) noexcept
	{
		SmallHash64 h(seed);
		(h.Append(xs), ...);
		return h.Value();
	}

} // namespace jam
