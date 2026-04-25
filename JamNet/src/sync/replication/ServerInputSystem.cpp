#include "pch.h"
#include "jamnet/sync/replication/ServerInputSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/NetWorldContext.h"
#include "jamnet/sync/networld/ServerNetWorld.h"
#include <cmath>

namespace jam::net
{
	namespace
	{
		constexpr uint32 kDirectionalMask =
			px::INPUT_FORWARD | px::INPUT_BACKWARD | px::INPUT_LEFT | px::INPUT_RIGHT | px::INPUT_RUN;

		bool TryResolveTargetPos(entt::registry& world, uint32 targetNetRaw, OUT px::Vec3& outPos)
		{
			if (targetNetRaw == 0)
				return false;

			const NetId targetNetId = NetId::MakeRaw(targetNetRaw);
			if (auto* nwPtr = world.ctx().find<ServerNetWorld*>(); nwPtr && *nwPtr)
			{
				const entt::entity e = (*nwPtr)->GetEntity(targetNetId);
				if (e != entt::null && world.valid(e))
				{
					if (const auto* cs = world.try_get<CharAuthorityState>(e))
					{
						outPos = cs->state.pos;
						return true;
					}

					if (const auto* rs = world.try_get<RigidAuthorityState>(e))
					{
						outPos = rs->state.pose.p;
						return true;
					}
				}
			}

			return false;
		}

		px::CharacterInput ResolveMouseMoveInput(
			entt::registry& world,
			const px::CharacterState* selfState,
			const px::CharacterInput& source)
		{
			px::CharacterInput resolved = source;

			const uint32 nonDirectional = source.inputFlags & ~kDirectionalMask;
			resolved.inputFlags = nonDirectional;

			if (!selfState)
				return resolved;

			px::Vec3 targetPos = source.targetPos;
			if (source.mouseMoveKind == px::eMouseMoveKind::FollowTarget)
			{
				if (!TryResolveTargetPos(world, source.targetNetId, targetPos))
				{
					// stop policy: follow target disappeared/unresolvable
					return resolved;
				}
			}

			px::Vec3 toTarget = targetPos - selfState->pos;
			toTarget.y = 0.0f;

			const float distSq = toTarget.MagnitudeSquared();
			constexpr float kStopRadius = 1.1f;
			if (distSq <= (kStopRadius * kStopRadius))
				return resolved;

			resolved.facingYaw = std::atan2(toTarget.x, toTarget.z);
			resolved.facingPitch = 0.0f;
			resolved.inputFlags |= px::INPUT_FORWARD;
			if (distSq > 100.0f)
				resolved.inputFlags |= px::INPUT_RUN;

			return resolved;
		}


	}

	ServerInputSystem::ServerInputSystem(entt::registry& world)
		: m_world(world)
	{
	}

	void ServerInputSystem::Init()
	{
		m_pendingInputs.clear();
		m_currentInputs.clear();
		m_appliedInputs.clear();
	}

	void ServerInputSystem::Tick()
	{
		DrainInputQueue();

		const uint32 serverTick = m_world.ctx().contains<TickCounter>()
			? m_world.ctx().get<TickCounter>().tick
			: 0;
		std::unordered_map<uint64, InputCmd> selectedInputs;

		auto view = m_world.view<ControlTag, px::CharacterInput>();
		for (auto e : view)
		{
			const auto& control = view.get<ControlTag>(e);
			
			InputCmd cmd{};
			if (auto it = selectedInputs.find(control.userId); it != selectedInputs.end())
			{
				cmd = it->second;
			}
			else
			{
				cmd = SelectInputForTick(control.userId);
				selectedInputs.emplace(control.userId, cmd);
			}

			auto& input = view.get<px::CharacterInput>(e);
			input = cmd.input;
			if (input.moveMode == px::eMoveInputMode::Mouse)
			{
				const px::CharacterState* selfState = nullptr;
				if (const auto* auth = m_world.try_get<CharAuthorityState>(e))
					selfState = &auth->state;
				input = ResolveMouseMoveInput(m_world, selfState, input);
			}

			cmd.input = input;
			m_currentInputs[control.userId] = cmd;
		}
	}

	void ServerInputSystem::EnqueueInput(uint64 userId, const InputCmd& cmd)
	{
		UserInputData data{ userId, cmd };
		m_inputQueue.enqueue(data);
	}

	void ServerInputSystem::MarkInputApplied(uint64 userId)
	{
		if (userId == 0)
			return;

		auto currentIt = m_currentInputs.find(userId);
		if (currentIt == m_currentInputs.end())
			return;

		auto& applied = m_appliedInputs[userId];
		if (currentIt->second.seq > applied.seq)
			applied = currentIt->second;
	}

	uint32 ServerInputSystem::LastAppliedSeq(uint64 userId) const
	{
		if (auto it = m_appliedInputs.find(userId); it != m_appliedInputs.end())
			return it->second.seq;
		return 0;
	}

	uint32 ServerInputSystem::LastAppliedCommandEpoch(uint64 userId) const
	{
		if (auto it = m_appliedInputs.find(userId); it != m_appliedInputs.end())
			return it->second.input.commandEpoch;
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
			QueuePendingInput(data.userId, data.cmd);
		}
	}

	void ServerInputSystem::QueuePendingInput(uint64 userId, const InputCmd& cmd)
	{
		if (userId == 0)
			return;

		const uint32 appliedSeq = LastAppliedSeq(userId);
		if (cmd.seq <= appliedSeq)
		{
			return;
		}

		auto& pending = m_pendingInputs[userId];
		auto insertIt = pending.end();
		for (auto it = pending.begin(); it != pending.end(); ++it)
		{
			if (it->seq == cmd.seq)
			{
				return;
			}
			if (it->seq > cmd.seq)
			{
				insertIt = it;
				break;
			}
		}

		pending.insert(insertIt, cmd);
	}

	InputCmd ServerInputSystem::SelectInputForTick(uint64 userId)
	{
		if (userId == 0)
			return {};

		auto& pending = m_pendingInputs[userId];
		const uint32 appliedSeq = LastAppliedSeq(userId);
		while (!pending.empty() && pending.front().seq <= appliedSeq)
		{
			pending.pop_front();
		}

		if (!pending.empty())
		{
			InputCmd cmd = pending.front();
			pending.pop_front();
			return cmd;
		}

		if (auto it = m_currentInputs.find(userId); it != m_currentInputs.end())
			return it->second;

		return {};
	}
}

