#pragma once
#include "jamnet/runtime/world/simulation/common/ReplicationTypes.h"
#include "jamnet/runtime/world/simulation/common/CharacterControlResolver.h"

namespace jam::net
{
	class ServerInputSystem
	{
public:
		ServerInputSystem(entt::registry& world);
		~ServerInputSystem() = default;

		void									Init();
		void									Tick();

		void									EnqueueInput(uint64 userId, const CharacterControlCommand& cmd);
		void									RemoveUser(uint64 userId);
		void									MarkInputApplied(uint64 userId);
		uint32									LastAppliedSeq(uint64 userId) const;
		uint32									LastAppliedControlRevision(uint64 userId) const;

private:
		void									QueuePendingInput(uint64 userId, const CharacterControlCommand& cmd);
		CharacterControlCommand								SelectInputForTick(uint64 userId);

private:
		entt::registry&							m_world;

		// Per user, keep the latest continuous state while accumulating
		// unconsumed edge actions until the next simulation tick.
		std::unordered_map<uint64, CharacterControlCommand>				m_pendingInputs;
		std::unordered_map<uint64, CharacterControlCommand>				m_currentInputs;
		std::unordered_map<uint64, CharacterControlCommand>				m_appliedInputs;
		CharacterControlResolveConfig					m_controlResolveConfig = {};
	};
}
