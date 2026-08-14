#include "pch.h"
#include "jamnet/runtime/world/simulation/server/ServerInputSystem.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/runtime/world/simulation/server/ServerWorld.h"

#include <cmath>

namespace jam::net
{
	namespace
	{
		bool IsFinite(const px::Vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsValidIntent(const CharacterControlIntent& intent)
		{
			if (!std::isfinite(intent.moveReferenceYaw) || !std::isfinite(intent.viewYaw) || !std::isfinite(intent.viewPitch))
				return false;
			if (intent.viewPolicy != eCharacterViewPolicy::FollowMovement
				&& intent.viewPolicy != eCharacterViewPolicy::Explicit)
				return false;
			return std::visit([](const auto& locomotion)
				{
					using T = std::decay_t<decltype(locomotion)>;
					if constexpr (std::is_same_v<T, DirectionalMoveIntent>)
						return std::isfinite(locomotion.localX) && std::isfinite(locomotion.localY)
							&& std::abs(locomotion.localX) <= 1.0f && std::abs(locomotion.localY) <= 1.0f;
					if constexpr (std::is_same_v<T, MoveToPositionIntent>) return IsFinite(locomotion.target);
					if constexpr (std::is_same_v<T, FollowActorIntent>) return locomotion.target.IsValid();
					return !std::holds_alternative<MoveByWorldRayIntent>(LocomotionIntent{ locomotion });
				}, intent.locomotion);
		}

		bool TryResolveTargetPos(entt::registry& world, uint32 targetActorRaw, OUT px::Vec3& outPos)
		{
			if (targetActorRaw == 0)
				return false;

			const ActorId targetActorId = ActorId(targetActorRaw);
			if (auto* nwPtr = world.ctx().find<ServerWorld*>(); nwPtr && *nwPtr)
			{
				const entt::entity e = (*nwPtr)->ResolveActor(targetActorId);
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
		m_latestControlRevisions.clear();
		m_moveToDiagnostics.clear();
	}

	void ServerInputSystem::Tick()
	{
		const uint32 serverTick = m_world.ctx().contains<TickCounter>()
			? m_world.ctx().get<TickCounter>().tick
			: 0;
		std::unordered_map<uint64, CharacterControlCommand> selectedInputs;

		auto view = m_world.view<ControlTag, px::CharacterMotorInput>();
		for (auto e : view)
		{
			const auto& control = view.get<ControlTag>(e);
			
			CharacterControlCommand cmd{};
			if (auto it = selectedInputs.find(control.userId); it != selectedInputs.end())
			{
				cmd = it->second;
			}
			else
			{
				cmd = SelectInputForTick(control.userId);
				selectedInputs.emplace(control.userId, cmd);
			}

			auto& input = view.get<px::CharacterMotorInput>(e);
			CharacterControlResolveContext context{};
			if (const auto* auth = m_world.try_get<CharAuthorityState>(e))
				context.selfState = &auth->state;
			if (const auto* follow = std::get_if<FollowActorIntent>(&cmd.intent.locomotion))
				context.hasFollowTargetPosition = TryResolveTargetPos(m_world, follow->target.Value(), context.followTargetPosition);
			input = CharacterControlResolver::Resolve(cmd.intent, context, m_controlResolveConfig);
			m_currentInputs[control.userId] = cmd;

			const auto* moveTo = std::get_if<MoveToPositionIntent>(&cmd.intent.locomotion);
			if (!moveTo || !context.selfState)
			{
				m_moveToDiagnostics.erase(control.userId);
				continue;
			}

			auto& diagnostic = m_moveToDiagnostics[control.userId];
			const px::Vec3 targetDelta = moveTo->target - diagnostic.target;
			if (!diagnostic.initialized || targetDelta.MagnitudeSquared() > 0.01f)
			{
				diagnostic.target = moveTo->target;
				diagnostic.lastPosition = context.selfState->pos;
				diagnostic.lastSampleTick = serverTick;
				diagnostic.stationarySamples = 0;
				diagnostic.initialized = true;
				diagnostic.reported = false;
				continue;
			}

			constexpr uint32 kDiagnosticSampleTicks = 30;
			if (serverTick - diagnostic.lastSampleTick < kDiagnosticSampleTicks)
				continue;

			px::Vec3 moved = context.selfState->pos - diagnostic.lastPosition;
			moved.y = 0.0f;
			px::Vec3 remaining = moveTo->target - context.selfState->pos;
			remaining.y = 0.0f;
			const float movedDistanceSq = moved.MagnitudeSquared();
			const float remainingDistanceSq = remaining.MagnitudeSquared();
			const bool expectsMovement = remainingDistanceSq > (m_controlResolveConfig.stopRadius * m_controlResolveConfig.stopRadius)
				&& (input.inputFlags & px::INPUT_FORWARD) != 0;
			if (expectsMovement && movedDistanceSq < 0.01f)
				diagnostic.stationarySamples = static_cast<uint8>(std::min<uint32>(diagnostic.stationarySamples + 1, 255));
			else
				diagnostic.stationarySamples = 0;

			diagnostic.lastPosition = context.selfState->pos;
			diagnostic.lastSampleTick = serverTick;
			if (!diagnostic.reported && diagnostic.stationarySamples >= 3)
			{
				uint64 worldId = 0;
				if (auto* world = m_world.ctx().find<ServerWorld*>(); world && *world)
					worldId = (*world)->GetWorldId();
				const auto pendingIt = m_pendingInputs.find(control.userId);
				const std::size_t pendingCount = pendingIt != m_pendingInputs.end() ? pendingIt->second.size() : 0;
				JAMNET_LOG_WARN(
					"[MoveToStallDiag] userId={}, worldId={}, tick={}, pos=({:.2f},{:.2f},{:.2f}), target=({:.2f},{:.2f},{:.2f}), remaining={:.2f}, moved1s={:.3f}, speed={:.3f}, flags=0x{:08X}, seq={}, revision={}, pending={}",
					control.userId, worldId, serverTick,
					context.selfState->pos.x, context.selfState->pos.y, context.selfState->pos.z,
					moveTo->target.x, moveTo->target.y, moveTo->target.z,
					std::sqrt(remainingDistanceSq), std::sqrt(movedDistanceSq), context.selfState->horizontalSpeed,
					input.inputFlags, cmd.sequence, cmd.intent.controlRevision, pendingCount);
				diagnostic.reported = true;
			}
		}
	}

	void ServerInputSystem::EnqueueInput(uint64 userId, const CharacterControlCommand& cmd)
	{
		QueuePendingInput(userId, cmd);
	}

	void ServerInputSystem::RemoveUser(uint64 userId)
	{
		if (userId == 0)
			return;

		m_pendingInputs.erase(userId);
		m_currentInputs.erase(userId);
		m_appliedInputs.erase(userId);
		m_latestControlRevisions.erase(userId);
		m_moveToDiagnostics.erase(userId);
	}

	void ServerInputSystem::MarkInputApplied(uint64 userId)
	{
		if (userId == 0)
			return;

		auto currentIt = m_currentInputs.find(userId);
		if (currentIt == m_currentInputs.end())
			return;

		auto& applied = m_appliedInputs[userId];
		if (currentIt->second.sequence > applied.sequence)
			applied = currentIt->second;
	}

	uint32 ServerInputSystem::LastAppliedSeq(uint64 userId) const
	{
		if (auto it = m_appliedInputs.find(userId); it != m_appliedInputs.end())
			return it->second.sequence;
		return 0;
	}

	uint32 ServerInputSystem::LastAppliedControlRevision(uint64 userId) const
	{
		if (auto it = m_appliedInputs.find(userId); it != m_appliedInputs.end())
			return it->second.intent.controlRevision;
		return 0;
	}

	void ServerInputSystem::QueuePendingInput(uint64 userId, const CharacterControlCommand& cmd)
	{
		if (userId == 0)
			return;
		if (!IsValidIntent(cmd.intent))
			return;

		const bool hasInputBaseline = m_appliedInputs.contains(userId)
			|| m_currentInputs.contains(userId)
			|| m_pendingInputs.contains(userId)
			|| m_latestControlRevisions.contains(userId);

		auto& latestRevision = m_latestControlRevisions[userId];
		if (cmd.intent.controlRevision < latestRevision)
			return;

		uint32 appliedSeq = LastAppliedSeq(userId);
		const auto pendingIt = m_pendingInputs.find(userId);
		const bool precedesPending = pendingIt != m_pendingInputs.end()
			&& !pendingIt->second.empty()
			&& cmd.sequence < pendingIt->second.begin()->first;
		const bool sequenceRestarted = cmd.intent.controlRevision > latestRevision
			&& (cmd.sequence <= appliedSeq || precedesPending);
		if (sequenceRestarted)
		{
			// A ClientWorld recreated for a new membership starts its input sequence
			// from zero. The runtime-wide control revision distinguishes it from late
			// packets belonging to the previous visit to the same WorldId.
			m_pendingInputs.erase(userId);
			m_currentInputs.erase(userId);
			m_appliedInputs.erase(userId);
		}

		if ((!hasInputBaseline || sequenceRestarted) && cmd.sequence > 1)
		{
			// The ClientWorld can tick while membership/player materialization is in
			// progress. Treat the first input that the ready server actor can receive as
			// the membership baseline instead of replaying the unavailable prefix as
			// synthetic Stop commands one slot per simulation tick.
			CharacterControlCommand baseline{};
			baseline.sequence = cmd.sequence - 1;
			m_appliedInputs.insert_or_assign(userId, baseline);
			appliedSeq = baseline.sequence;
		}
		else if (sequenceRestarted)
		{
			appliedSeq = 0;
		}
		latestRevision = cmd.intent.controlRevision;

		if (cmd.sequence <= appliedSeq)
		{
			return;
		}

		auto& pending = m_pendingInputs[userId];
		if (auto it = pending.find(cmd.sequence); it != pending.end())
		{
			const CharacterActionFlags accumulatedEdges = it->second.intent.edgeActions;
			it->second = cmd;
			it->second.intent.edgeActions |= accumulatedEdges;
			return;
		}

		if (pending.size() >= kMaxPendingInputsPerUser)
			return;

		pending.emplace(cmd.sequence, cmd);
	}

	CharacterControlCommand ServerInputSystem::SelectInputForTick(uint64 userId)
	{
		if (userId == 0)
			return {};

		const uint32 appliedSeq = LastAppliedSeq(userId);
		const uint32 nextSeq = appliedSeq + 1;
		if (auto pendingIt = m_pendingInputs.find(userId); pendingIt != m_pendingInputs.end())
		{
			auto& pending = pendingIt->second;
			pending.erase(pending.begin(), pending.upper_bound(appliedSeq));

			if (auto commandIt = pending.find(nextSeq); commandIt != pending.end())
			{
				CharacterControlCommand cmd = commandIt->second;
				pending.erase(commandIt);
				if (pending.empty())
					m_pendingInputs.erase(pendingIt);
				return cmd;
			}

			// A newer command proves that the client advanced through nextSeq. Fill
			// the missing unreliable packet with the last continuous state so ACK and
			// client prediction history retain one-step-per-sequence semantics.
			if (!pending.empty() && pending.begin()->first > nextSeq)
			{
				CharacterControlCommand cmd{};
				if (auto currentIt = m_currentInputs.find(userId); currentIt != m_currentInputs.end())
					cmd = currentIt->second;
				cmd.sequence = nextSeq;
				cmd.intent.edgeActions = 0;
				return cmd;
			}
		}

		if (auto it = m_currentInputs.find(userId); it != m_currentInputs.end())
		{
			CharacterControlCommand cmd = it->second;
			cmd.intent.edgeActions = 0;
			return cmd;
		}

		return {};
	}
}
