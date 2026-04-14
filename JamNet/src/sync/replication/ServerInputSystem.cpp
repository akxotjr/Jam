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

		float AbsAngleDelta(float a, float b)
		{
			float d = std::fmod(a - b, px::TWO_PI);
			if (d > px::PI)  d -= px::TWO_PI;
			if (d < -px::PI) d += px::TWO_PI;
			return std::abs(d);
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
		m_lastAppliedLogs.clear();
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
				input = ResolveMouseMoveInput(m_world, m_world.try_get<px::CharacterState>(e), input);

			cmd.input = input;
			m_currentInputs[control.userId] = cmd;

			auto& lastLog = m_lastAppliedLogs[control.userId];
			const bool firstLog = lastLog.tick == 0;
			const uint32 seqGap = (cmd.seq > lastLog.seq) ? (cmd.seq - lastLog.seq) : 0;
			const bool seqJumped = !firstLog && seqGap > 1;
			const bool reusedSeq = !firstLog && cmd.seq == lastLog.seq && serverTick > lastLog.tick;
			const bool flagsChanged = firstLog || input.inputFlags != lastLog.flags;
			const bool epochChanged = firstLog || input.commandEpoch != lastLog.epoch;
			const bool modeChanged = firstLog || E2U(input.moveMode) != lastLog.mode;
			const bool yawChanged = firstLog || AbsAngleDelta(input.facingYaw, lastLog.yaw) > 0.01f;

			//if (firstLog || seqJumped || reusedSeq || flagsChanged || epochChanged || modeChanged || yawChanged)
			//{
			//	JAMNET_LOG_DEBUG(
			//		"[ServerInputApply] tick={}, userId={}, entity={}, seq={}, prevSeq={}, seqGap={}, reusedSeq={}, flags={}, yaw={}, epoch={}, mode={}, target=({}, {}, {}), targetNetId={}",
			//		serverTick,
			//		control.userId,
			//		static_cast<uint32>(e),
			//		cmd.seq,
			//		lastLog.seq,
			//		seqGap,
			//		reusedSeq,
			//		input.inputFlags,
			//		input.facingYaw,
			//		input.commandEpoch,
			//		E2U(input.moveMode),
			//		input.targetPos.x,
			//		input.targetPos.y,
			//		input.targetPos.z,
			//		input.targetNetId);
			//}

			lastLog.seq = cmd.seq;
			lastLog.flags = input.inputFlags;
			lastLog.epoch = input.commandEpoch;
			lastLog.yaw = input.facingYaw;
			lastLog.mode = E2U(input.moveMode);
			lastLog.tick = serverTick;
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
			//JAMNET_LOG_DEBUG(
			//	"[ServerInputDrainDrop] userId={}, droppedSeq={}, appliedSeq={}, reason=already_applied, flags={}, yaw={}, epoch={}, mode={}",
			//	userId,
			//	cmd.seq,
			//	appliedSeq,
			//	cmd.input.inputFlags,
			//	cmd.input.facingYaw,
			//	cmd.input.commandEpoch,
			//	E2U(cmd.input.moveMode));
			return;
		}

		auto& pending = m_pendingInputs[userId];
		auto insertIt = pending.end();
		for (auto it = pending.begin(); it != pending.end(); ++it)
		{
			if (it->seq == cmd.seq)
			{
				JAMNET_LOG_DEBUG(
					"[ServerInputDrainDrop] userId={}, droppedSeq={}, reason=duplicate_pending, flags={}, yaw={}, epoch={}, mode={}",
					userId,
					cmd.seq,
					cmd.input.inputFlags,
					cmd.input.facingYaw,
					cmd.input.commandEpoch,
					E2U(cmd.input.moveMode));
				return;
			}
			if (it->seq > cmd.seq)
			{
				insertIt = it;
				break;
			}
		}

		const uint32 prevQueuedSeq = pending.empty()
			? appliedSeq
			: pending.back().seq;
		const uint32 seqGap = (prevQueuedSeq == 0 || cmd.seq <= prevQueuedSeq) ? 0 : (cmd.seq - prevQueuedSeq);

		InputCmd prevForLog{};
		bool hasPrevForLog = false;
		if (!pending.empty())
		{
			prevForLog = pending.back();
			hasPrevForLog = true;
		}
		else if (auto it = m_currentInputs.find(userId); it != m_currentInputs.end())
		{
			prevForLog = it->second;
			hasPrevForLog = true;
		}

		const bool firstInput = !hasPrevForLog;
		const bool seqJumped = seqGap > 1;
		const bool outOfOrder = insertIt != pending.end();
		const bool flagsChanged = firstInput || cmd.input.inputFlags != prevForLog.input.inputFlags;
		const bool epochChanged = firstInput || cmd.input.commandEpoch != prevForLog.input.commandEpoch;
		const bool modeChanged = firstInput || cmd.input.moveMode != prevForLog.input.moveMode;
		const bool yawChanged = firstInput || AbsAngleDelta(cmd.input.facingYaw, prevForLog.input.facingYaw) > 0.01f;

		if (firstInput || seqJumped || outOfOrder || flagsChanged || epochChanged || modeChanged || yawChanged)
		{
			//JAMNET_LOG_DEBUG(
			//	"[ServerInputDrain] userId={}, prevQueuedSeq={}, newSeq={}, seqGap={}, outOfOrder={}, pendingBefore={}, flags={}, yaw={}, epoch={}, mode={}, target=({}, {}, {}), targetNetId={}",
			//	userId,
			//	prevQueuedSeq,
			//	cmd.seq,
			//	seqGap,
			//	outOfOrder,
			//	pending.size(),
			//	cmd.input.inputFlags,
			//	cmd.input.facingYaw,
			//	cmd.input.commandEpoch,
			//	E2U(cmd.input.moveMode),
			//	cmd.input.targetPos.x,
			//	cmd.input.targetPos.y,
			//	cmd.input.targetPos.z,
			//	cmd.input.targetNetId);
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
			JAMNET_LOG_DEBUG(
				"[ServerInputConsumeDrop] userId={}, droppedSeq={}, appliedSeq={}, reason=stale_pending",
				userId,
				pending.front().seq,
				appliedSeq);
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

