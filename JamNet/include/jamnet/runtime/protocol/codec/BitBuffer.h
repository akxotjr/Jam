#pragma once

#include <array>
#include <algorithm>
#include <limits>
#include <type_traits>

namespace jam::net
{
	template <typename WordT, size_t WordN>
	class BitBuffer
	{
		static_assert(WordN > 0, "BitBuffer requires at least one word");
		static_assert(std::is_integral_v<WordT>, "BitBuffer word type must be integral");
		static_assert(std::is_unsigned_v<WordT>, "BitBuffer word type must be unsigned");

	public:
		static constexpr size_t WordCount = WordN;
		static constexpr int32  WordBits  = static_cast<int32>(sizeof(WordT) * 8);
		static constexpr int32  TotalBits = static_cast<int32>(WordN * sizeof(WordT) * 8);

	public:
		BitBuffer() = default;

		explicit BitBuffer(const std::array<WordT, WordN>& words) : m_words(words) {}

		void								Clear()	{ m_words.fill(0); m_bitCursor = 0; }
		void								ResetCursor() { m_bitCursor = 0; }
		bool								Seek(int32 bitCursor);

		int32								Cursor() const { return m_bitCursor; }
		const std::array<WordT, WordN>&		Words()  const { return m_words; }
		WordT								Word(size_t index) const { return m_words[index]; }

		bool								WriteBits(uint64 value, int32 bitCount);
		bool								ReadBits(int32 bitCount, OUT uint64& outValue);

	private:
		static constexpr uint64				Mask64(int32 bitCount);
		static constexpr uint64				ExtractBits(WordT value, int32 bitOffset, int32 bitCount);

	private:
		std::array<WordT, WordN>		m_words = {};
		int32							m_bitCursor  = 0;
	};


	template <typename WordT, size_t WordN>
	bool BitBuffer<WordT, WordN>::Seek(int32 bitCursor)
	{
		if (bitCursor < 0 || bitCursor > TotalBits)
			return false;

		m_bitCursor = bitCursor;
		return true;
	}

	template <typename WordT, size_t WordN>
	bool BitBuffer<WordT, WordN>::WriteBits(uint64 value, int32 bitCount)
	{
		if (bitCount < 0 || bitCount > 64)
			return false;
		if ((m_bitCursor + bitCount) > TotalBits)
			return false;

		int32 remaining = bitCount;
		uint64 v = value;

		while (remaining > 0)
		{
			const int32 wordIndex = m_bitCursor / WordBits;
			const int32 inWord = m_bitCursor % WordBits;
			const int32 canWrite = std::min<int32>(remaining, WordBits - inWord);

			const uint64 chunk = v & Mask64(canWrite);
			m_words[wordIndex] |= static_cast<WordT>(chunk) << inWord;

			v >>= canWrite;
			remaining -= canWrite;
			m_bitCursor += canWrite;
		}

		return true;
	}

	template <typename WordT, size_t WordN>
	bool BitBuffer<WordT, WordN>::ReadBits(int32 bitCount, uint64& outValue)
	{
		if (bitCount < 0 || bitCount > 64)
			return false;
		if ((m_bitCursor + bitCount) > TotalBits)
			return false;

		int32  remaining = bitCount;
		int32  outShift = 0;
		uint64 out = 0;

		while (remaining > 0)
		{
			const int32 wordIndex = m_bitCursor / WordBits;
			const int32 inWord = m_bitCursor % WordBits;
			const int32 canRead = std::min<int32>(remaining, WordBits - inWord);

			const uint64 chunk = ExtractBits(m_words[wordIndex], inWord, canRead);
			out |= (chunk << outShift);

			remaining -= canRead;
			m_bitCursor += canRead;
			outShift += canRead;
		}

		outValue = out;
		return true;
	}

	template <typename WordT, size_t WordN>
	constexpr uint64 BitBuffer<WordT, WordN>::Mask64(int32 bitCount)
	{
		if (bitCount <= 0)
			return 0;
		if (bitCount >= 64)
			return std::numeric_limits<uint64>::max();
		return (1ull << bitCount) - 1ull;
	}

	template <typename WordT, size_t WordN>
	constexpr uint64 BitBuffer<WordT, WordN>::ExtractBits(WordT value, int32 bitOffset, int32 bitCount)
	{
		if (bitCount <= 0)
			return 0;
		if (bitCount >= 64)
			return static_cast<uint64>(value) >> bitOffset;
		return (static_cast<uint64>(value) >> bitOffset) & Mask64(bitCount);
	}
}
