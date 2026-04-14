#pragma once
#include "ReplicationTypes.h"

namespace jam::net
{
	using namespace moodycamel;

	struct UserInputData
	{
		uint64		userId;
		InputCmd	cmd;
	};

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
		struct AppliedInputLogState
		{
			uint32 seq = 0;
			uint32 flags = 0;
			uint32 epoch = 0;
			float yaw = 0.0f;
			uint8 mode = 0;
			uint32 tick = 0;
		};

		void									DrainInputQueue();
		void									QueuePendingInput(uint64 userId, const InputCmd& cmd);
		InputCmd								SelectInputForTick(uint64 userId);

	private:
		entt::registry&							m_world;

		// Lock-Free Queue (네트워크 스레드 -> 게임 루프)
		ConcurrentQueue<UserInputData>			m_inputQueue;

		std::unordered_map<uint64, std::deque<InputCmd>>	m_pendingInputs;
		std::unordered_map<uint64, InputCmd>				m_currentInputs;
		std::unordered_map<uint64, InputCmd>	m_appliedInputs;
		std::unordered_map<uint64, AppliedInputLogState>	m_lastAppliedLogs;
	};
}

