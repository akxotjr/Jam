#pragma once
#include "jamnet/runtime/world/simulation/common/ReplicationTypes.h"
#include "jamnet/runtime/world/simulation/common/CharacterControlResolver.h"

#include <cstddef>
#include <map>
#include <optional>
#include <span>

namespace jam::net
{
	class ServerWorld;

	struct ServerUserInputState
	{
		std::map<uint32, CharacterControlCommand>	pending;
		std::optional<CharacterControlCommand>		current;
		std::optional<CharacterControlCommand>		applied;
		uint32								latestControlRevision = 0;
	};

	class ServerInputSystem
	{
	public:
		ServerInputSystem(ServerWorld& world, entt::registry& registry);
		~ServerInputSystem() = default;

		void									Init();
		void									Tick();

		void									EnqueueInput(uint64 userId, const CharacterControlCommand& cmd);
		void									ResetInputStream(uint64 userId);
		void									MarkInputApplied(std::span<const uint64> userIds);
		uint32									LastAppliedSeq(uint64 userId) const;
		uint32									LastAppliedControlRevision(uint64 userId) const;

	private:
		void									QueuePendingInput(uint64 userId, const CharacterControlCommand& cmd);
		CharacterControlCommand					SelectInputForTick(uint64 userId);

	private:
		static constexpr std::size_t kMaxPendingInputsPerUser = 256;

		ServerWorld&				m_world;
		entt::registry&				m_registry;

		// One input stream exists per user. Preserve client sequence order so one
		// server simulation step consumes exactly one logical client input slot.
		// Missing slots are synthesized from the held continuous state once a newer
		// sequence proves that the slot exists.
		std::unordered_map<uint64, ServerUserInputState>				m_userInputs;
		CharacterControlResolveConfig					m_controlResolveConfig = {};
	};
}
