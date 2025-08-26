#pragma once
#include <chrono>

namespace jam::utils
{
	struct FractionalTick
	{
		uint64 tick;     // 
		double alpha;    // [0, 1)
	};

	class Clock
	{
		DECLARE_SINGLETON(Clock)

	public:
		void			Start(uint32 tickHz);

		// 절대 단조 시간(부트 원점 기준)
		uint64			NowNs() const;
		uint64			NowUs() const;
		uint64			NowMs() const;
		uint64			NowSec() const;

		// Start() 이후 경과
		uint64			ElapsedNs() const;
		uint64			ElapsedUs() const;
		uint64			ElapsedMs() const;
		uint64			ElapsedSec() const;


		FractionalTick NowFractionalTick() const;
		FractionalTick ElapsedFractionalTick() const;


		template<class Dur>
		Dur NowChrono() const
		{
			return std::chrono::duration_cast<Dur>(std::chrono::nanoseconds(NowNs()));
		}
		template<class Dur>
		Dur ElapsedChrono() const
		{
			return std::chrono::duration_cast<Dur>(std::chrono::nanoseconds(ElapsedNs()));
		}

	private:

		inline int64 ReadQpc() const
		{
			LARGE_INTEGER counter;
			::QueryPerformanceCounter(&counter);
			return counter.QuadPart;
		}

		inline uint64 QpcToNs(int64 counter) const
		{
			assert(counter >= 0);                // 여기서는 항상 델타라서 0 이상이어야 정상
			const uint64 c = static_cast<uint64>(counter);
			const uint64 f = static_cast<uint64>(m_qpcFreq);
			// (c * 1e9) / f  를 128-bit 정수로 안전히 계산
			uint64 hi;
			const uint64 lo = _umul128(c, 1'000'000'000ULL, &hi);
			return _udiv128(hi, lo, f, nullptr); // 몫(나노초) 반환
		}


		inline uint64 NowAbsNs() const
		{
			return QpcToNs(ReadQpc() - m_qpcAtBoot);
		}

		inline uint64 ElapsedAbsNs() const
		{
			return QpcToNs(ReadQpc() - m_qpcAtStart);
		}

	private:

		int64 m_qpcFreq{ 0 };
		int64 m_qpcAtBoot{ 0 };
		int64 m_qpcAtStart{ 0 };

		uint32 m_tickHz{ 0 };
		uint64 m_tickIntervalNs{ 0 };
	};
}