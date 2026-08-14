#pragma once

#include <cstdint>

namespace m1::shared
{
	inline constexpr uint64_t kBotAccountBegin = 6000;
	inline constexpr uint64_t kBotAccountEnd = 9999;

	inline bool IsBotAccount(uint64_t accountId) noexcept
	{
		return accountId >= kBotAccountBegin && accountId <= kBotAccountEnd;
	}

	struct BotTraversePlacement
	{
		uint32_t laneIndex = 0;
		float phase = 0.5f;
		bool reverse = false;
		uint32_t startDelayMs = 0;
	};

	inline BotTraversePlacement MakeBotTraversePlacement(uint64_t accountId, uint32_t laneCount) noexcept
	{
		const uint64_t ordinal = accountId >= kBotAccountBegin ? accountId - kBotAccountBegin : accountId;
		const uint64_t hash = ordinal * 2654435761ull;
		const float phase = 0.25f + static_cast<float>(hash % 10'000) * 0.5f / 10'000.0f;
		return {
			.laneIndex = laneCount != 0 ? static_cast<uint32_t>(ordinal % laneCount) : 0,
			.phase = phase,
			.reverse = ((hash / 50'000) & 1ull) != 0,
			.startDelayMs = static_cast<uint32_t>((hash / 10'000) % 5'000),
		};
	}
}
