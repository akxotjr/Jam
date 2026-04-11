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
		void									DrainInputQueue();

	private:
		entt::registry&							m_world;

		// Lock-Free Queue (네트워크 스레드 -> 게임 루프)
		ConcurrentQueue<UserInputData>			m_inputQueue;

		// 유저별 최신 입력 (deque 제거)
		std::unordered_map<uint64, InputCmd>	m_latestInputs;
		std::unordered_map<uint64, InputCmd>	m_appliedInputs;
	};
}

