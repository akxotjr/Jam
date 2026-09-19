#pragma once

#include <cassert>
#include <compare>
#include <cstdint>
#include <limits>

namespace jam
{
	class Duration
	{
	public:
		using Rep = std::uint64_t;

		static constexpr Rep NsPerUs  = 1'000ull;
		static constexpr Rep NsPerMs  = 1'000'000ull;
		static constexpr Rep NsPerSec = 1'000'000'000ull;

	public:
		constexpr Duration() noexcept = default;

		[[nodiscard]]
		static constexpr Duration FromNs(Rep ns) noexcept { return Duration(ns); }

		[[nodiscard]]
		static constexpr Duration FromUs(Rep us) noexcept
		{
			assert(us <= MaxRep() / NsPerUs);
			return Duration(us * NsPerUs);
		}

		[[nodiscard]]
		static constexpr Duration FromMs(Rep ms) noexcept
		{
			assert(ms <= MaxRep() / NsPerMs);
			return Duration(ms * NsPerMs);
		}

		[[nodiscard]]
		static constexpr Duration FromSec(Rep sec) noexcept
		{
			assert(sec <= MaxRep() / NsPerSec);
			return Duration(sec * NsPerSec);
		}

		[[nodiscard]]
		static constexpr Duration Zero() noexcept { return Duration(0); }

		[[nodiscard]]
		static constexpr Duration Max() noexcept { return Duration(std::numeric_limits<Rep>::max()); }


		[[nodiscard]]
		constexpr Rep Ns() const noexcept { return m_ns; }

		[[nodiscard]]
		constexpr Rep Us() const noexcept { return m_ns / NsPerUs; }

		[[nodiscard]]
		constexpr Rep Ms() const noexcept { return m_ns / NsPerMs; }

		[[nodiscard]]
		constexpr Rep Sec() const noexcept { return m_ns / NsPerSec; }


		[[nodiscard]]
		constexpr std::uint32_t Us32() const noexcept { return static_cast<std::uint32_t>(Us()); }

		[[nodiscard]]
		constexpr std::uint32_t Ms32() const noexcept { return static_cast<std::uint32_t>(Ms()); }

		[[nodiscard]]
		constexpr std::uint32_t Sec32() const noexcept { return static_cast<std::uint32_t>(Sec()); }


		[[nodiscard]]
		constexpr bool IsZero() const noexcept { return m_ns == 0; }


		[[nodiscard]]
		constexpr auto operator<=>(const Duration&) const noexcept = default;


		constexpr Duration& operator+=(Duration rhs) noexcept
		{
			assert(rhs.m_ns <= MaxRep() - m_ns);
			m_ns += rhs.m_ns;
			return *this;
		}

		constexpr Duration& operator-=(Duration rhs) noexcept
		{
			assert(rhs.m_ns <= m_ns);
			m_ns -= rhs.m_ns;
			return *this;
		}

		constexpr Duration& operator*=(Rep scalar) noexcept
		{
			assert(m_ns == 0 || scalar <= MaxRep() / m_ns);
			m_ns *= scalar;
			return *this;
		}

		constexpr Duration& operator/=(Rep scalar) noexcept
		{
			assert(scalar != 0);
			m_ns /= scalar;
			return *this;
		}

		[[nodiscard]]
		constexpr double Ratio(Duration rhs) const noexcept
		{
			assert(!rhs.IsZero());
			return static_cast<double>(m_ns) / static_cast<double>(rhs.m_ns);
		}


		[[nodiscard]]
		friend constexpr Duration operator+(Duration lhs, Duration rhs) noexcept
		{
			lhs += rhs;
			return lhs;
		}

		[[nodiscard]]
		friend constexpr Duration operator-(Duration lhs, Duration rhs) noexcept
		{
			lhs -= rhs;
			return lhs;
		}

		[[nodiscard]]
		friend constexpr Duration operator*(Duration time, Rep scalar) noexcept
		{
			time *= scalar;
			return time;
		}

		[[nodiscard]]
		friend constexpr Duration operator*(Rep scalar, Duration time) noexcept
		{
			time *= scalar;
			return time;
		}

		[[nodiscard]]
		friend constexpr Duration operator/(Duration time, Rep scalar) noexcept
		{
			time /= scalar;
			return time;
		}

		[[nodiscard]]
		friend constexpr Rep operator/(Duration lhs, Duration rhs) noexcept
		{
			assert(!rhs.IsZero());
			return lhs.m_ns / rhs.m_ns;
		}

	private:
		static constexpr Rep MaxRep() noexcept { return std::numeric_limits<Rep>::max(); }

		explicit constexpr Duration(Rep ns) noexcept : m_ns(ns) {}

	private:
		Rep m_ns = 0;
	};


	class Timestamp
	{
	public:
		using Rep = std::uint64_t;

		static constexpr Rep NsPerUs  = 1'000ull;
		static constexpr Rep NsPerMs  = 1'000'000ull;
		static constexpr Rep NsPerSec = 1'000'000'000ull;

	public:
		constexpr Timestamp() noexcept = default;

		[[nodiscard]]
		static constexpr Timestamp FromNs(Rep ns) noexcept { return Timestamp(ns); }

		[[nodiscard]]
		static constexpr Timestamp FromUs(Rep us) noexcept
		{
			assert(us <= MaxRep() / NsPerUs);
			return Timestamp(us * NsPerUs);
		}

		[[nodiscard]]
		static constexpr Timestamp FromMs(Rep ms) noexcept
		{
			assert(ms <= MaxRep() / NsPerMs);
			return Timestamp(ms * NsPerMs);
		}

		[[nodiscard]]
		static constexpr Timestamp FromSec(Rep sec) noexcept
		{
			assert(sec <= MaxRep() / NsPerSec);
			return Timestamp(sec * NsPerSec);
		}

		[[nodiscard]]
		static constexpr Timestamp Zero() noexcept { return Timestamp(0); }

		[[nodiscard]]
		static constexpr Timestamp Max() noexcept { return Timestamp(std::numeric_limits<Rep>::max()); }


		[[nodiscard]]
		constexpr Rep Ns() const noexcept { return m_ns; }

		[[nodiscard]]
		constexpr Rep Us() const noexcept { return m_ns / NsPerUs; }

		[[nodiscard]]
		constexpr Rep Ms() const noexcept { return m_ns / NsPerMs; }

		[[nodiscard]]
		constexpr Rep Sec() const noexcept { return m_ns / NsPerSec; }


		[[nodiscard]]
		constexpr std::uint32_t Us32() const noexcept { return static_cast<std::uint32_t>(Us()); }

		[[nodiscard]]
		constexpr std::uint32_t Ms32() const noexcept { return static_cast<std::uint32_t>(Ms()); }

		[[nodiscard]]
		constexpr std::uint32_t Sec32() const noexcept { return static_cast<std::uint32_t>(Sec()); }


		[[nodiscard]]
		constexpr bool IsZero() const noexcept { return m_ns == 0; }


		[[nodiscard]]
		constexpr auto operator<=>(const Timestamp&) const noexcept = default;


		constexpr Timestamp& operator+=(Duration duration) noexcept
		{
			assert(duration.Ns() <= MaxRep() - m_ns);
			m_ns += duration.Ns();
			return *this;
		}

		constexpr Timestamp& operator-=(Duration duration) noexcept
		{
			assert(duration.Ns() <= m_ns);
			m_ns -= duration.Ns();
			return *this;
		}

		[[nodiscard]]
		constexpr Timestamp SaturatingSub(Duration duration) const noexcept
		{
			return duration.Ns() >= m_ns ? Zero() : Timestamp(m_ns - duration.Ns());
		}


		[[nodiscard]]
		friend constexpr Timestamp operator+(Timestamp timestamp, Duration duration) noexcept
		{
			timestamp += duration;
			return timestamp;
		}

		[[nodiscard]]
		friend constexpr Timestamp operator+(Duration duration, Timestamp timestamp) noexcept
		{
			timestamp += duration;
			return timestamp;
		}

		[[nodiscard]]
		friend constexpr Timestamp operator-(Timestamp timestamp, Duration duration) noexcept
		{
			timestamp -= duration;
			return timestamp;
		}

		[[nodiscard]]
		friend constexpr Duration operator-(Timestamp lhs, Timestamp rhs) noexcept
		{
			assert(rhs.m_ns <= lhs.m_ns);
			return Duration::FromNs(lhs.m_ns - rhs.m_ns);
		}

	private:
		static constexpr Rep MaxRep() noexcept { return std::numeric_limits<Rep>::max(); }

		explicit constexpr Timestamp(Rep ns) noexcept : m_ns(ns) {}

	private:
		Rep m_ns = 0;
	};





	enum class eTimeUnit
	{
		Ns,
		Us,
		Ms,
		Sec,
	};

	constexpr Duration operator""_ns(unsigned long long v) { return Duration::FromNs(static_cast<Duration::Rep>(v)); }
	constexpr Duration operator""_us(unsigned long long v) { return Duration::FromUs(static_cast<Duration::Rep>(v)); }
	constexpr Duration operator""_ms(unsigned long long v) { return Duration::FromMs(static_cast<Duration::Rep>(v)); }
	constexpr Duration operator""_s(unsigned long long v)  { return Duration::FromSec(static_cast<Duration::Rep>(v)); }


	constexpr Duration Ns(Duration::Rep v)  { return Duration::FromNs(v); }
	constexpr Duration Us(Duration::Rep v)  { return Duration::FromUs(v); }
	constexpr Duration Ms(Duration::Rep v)  { return Duration::FromMs(v); }
	constexpr Duration Sec(Duration::Rep v) { return Duration::FromSec(v); }
}
