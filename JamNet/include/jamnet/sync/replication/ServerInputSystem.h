#pragma once
#include "ReplicationTypes.h"

namespace jam::net
{
	class ServerInputSystem
	{
public:
		ServerInputSystem(entt::registry& world);
		~ServerInputSystem() = default;

		void									Init();
		void									Tick();

		void									EnqueueInput(uint64 userId, const InputCmd& cmd);
		void									MarkInputApplied(uint64 userId);
		uint32									LastAppliedSeq(uint64 userId) const;
		uint32									LastAppliedCommandEpoch(uint64 userId) const;

private:
		void									QueuePendingInput(uint64 userId, const InputCmd& cmd);
		InputCmd								SelectInputForTick(uint64 userId);

private:
		entt::registry&							m_world;

		std::unordered_map<uint64, std::deque<InputCmd>>	m_pendingInputs;
		std::unordered_map<uint64, InputCmd>				m_currentInputs;
		std::unordered_map<uint64, InputCmd>				m_appliedInputs;
	};
}
