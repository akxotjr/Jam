#pragma once

#include <jambase/JamTypes.h>

namespace jam
{
	constexpr uint64 operator""_ns(unsigned long long v) { return static_cast<uint64>(v); }
	constexpr uint64 operator""_us(unsigned long long v) { return static_cast<uint64>(v) * 1'000ull; }
	constexpr uint64 operator""_ms(unsigned long long v) { return static_cast<uint64>(v) * 1'000'000ull; }
	constexpr uint64 operator""_s(unsigned long long v)  { return static_cast<uint64>(v) * 1'000'000'000ull; }


	constexpr uint64 Ns(uint64 v)  { return v; }
	constexpr uint64 Us(uint64 v)  { return v * 1'000ull; }
	constexpr uint64 Ms(uint64 v)  { return v * 1'000'000ull; }
	constexpr uint64 Sec(uint64 v) { return v * 1'000'000'000ull; }
}
