#include "pch.h"
#include "jamnet/sync/replication/ServerInputSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"
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
			auto view = world.view<NetId>();
			for (const entt::entity e : view)
			{
				if (view.get<NetId>(e) != targetNetId)
					continue;

				if (const auto* cs = world.try_get<px::CharacterState>(e))
				{
					outPos = cs->pos;
					return true;
				}

				if (const auto* rs = world.try_get<px::RigidState>(e))
				{
					outPos = rs->pose.p;
					return true;
				}
				return false;
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
		m_latestInputs.clear();
		m_appliedInputs.clear();
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
			if (input.moveMode == px::eMoveInputMode::Mouse)
				input = ResolveMouseMoveInput(m_world, m_world.try_get<px::CharacterState>(e), input);
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

		auto latestIt = m_latestInputs.find(userId);
		if (latestIt == m_latestInputs.end())
			return;

		auto& applied = m_appliedInputs[userId];
		if (latestIt->second.seq > applied.seq)
			applied = latestIt->second;
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
			
			// 최신 입력으로 덮어쓰기 (Out-of-Order 체크)
			auto& latest = m_latestInputs[data.userId];
			if (data.cmd.seq > latest.seq)
			{
				latest = data.cmd;
			}
		}
	}
}

