#include "pch.h"
#include "jamnet/sync/replication/ServerInputSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"

namespace jam::net
{
	ServerInputSystem::ServerInputSystem(entt::registry& world)
		: m_world(world)
	{
	}

	void ServerInputSystem::Init()
	{
		m_latestInputs.clear();
	}

	void ServerInputSystem::Tick()
	{
		DrainInputQueue();

		auto view = m_world.view<ControlTag, px::CharacterInput>();
		for (auto e : view)
		{
			const auto& control = view.get<ControlTag>(e);
			
			InputCmd cmd{};
			if (auto it = m_latestInputs.find(control.userId); it != m_latestInputs.end())
			{
				cmd = it->second;
			}

			auto& input = view.get<px::CharacterInput>(e);
			input = cmd.input;
		}
	}

	void ServerInputSystem::EnqueueInput(uint64 userId, const InputCmd& cmd)
	{
		UserInputData data{ userId, cmd };
		m_inputQueue.enqueue(data);
	}

	uint32 ServerInputSystem::LastProcessedSeq(uint64 userId) const
	{
		if (auto it = m_latestInputs.find(userId); it != m_latestInputs.end())
			return it->second.seq;
		return 0;
	}

	void ServerInputSystem::DrainInputQueue()
	{
		constexpr size_t BULK_SIZE = 256;
		UserInputData batch[BULK_SIZE];

		size_t count = m_inputQueue.try_dequeue_bulk(batch, BULK_SIZE);

		for (size_t i = 0; i < count; ++i)
		{
			const auto& data = batch[i];
			
			// 최신 입력으로 덮어쓰기 (Out-of-Order 체크)
			auto& latest = m_latestInputs[data.userId];
			if (data.cmd.seq > latest.seq)
			{
				latest = data.cmd;
			}
		}
	}
}
