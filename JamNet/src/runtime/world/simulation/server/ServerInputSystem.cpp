#include "pch.h"
#include "jamnet/runtime/world/simulation/server/ServerInputSystem.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
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

			if (intent.viewPolicy != eCharacterViewPolicy::FollowMovement && intent.viewPolicy != eCharacterViewPolicy::Explicit)
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

		bool TryResolveTargetPos(entt::registry& world, ServerWorld& serverWorld, uint32 targetActorRaw, OUT px::Vec3& outPos)
		{
			if (targetActorRaw == 0)
				return false;

			const ActorId targetActorId = ActorId(targetActorRaw);
			const entt::entity e = serverWorld.ResolveActor(targetActorId);
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

			return false;
		}

	}

	ServerInputSystem::ServerInputSystem(entt::registry& world, ServerWorld& serverWorld)
		: m_world(world), m_serverWorld(serverWorld)
	{
	}

	void ServerInputSystem::Init()
	{
		m_userInputs.clear();
	}

	void ServerInputSystem::Tick()
	{
		auto view = m_world.view<ControlTag, px::CharacterMotorInput>();
		for (auto e : view)
		{
			const auto& control = view.get<ControlTag>(e);
			JAM_ASSERT(control.userId != 0);
			if (control.userId == 0)
				continue;

			const CharacterControlCommand cmd = SelectInputForTick(control.userId);

			auto& input = view.get<px::CharacterMotorInput>(e);
			CharacterControlResolveContext context{};
			if (const auto* auth = m_world.try_get<CharAuthorityState>(e))
				context.selfState = &auth->state;

			if (const auto* follow = std::get_if<FollowActorIntent>(&cmd.intent.locomotion))
				context.hasFollowTargetPosition = TryResolveTargetPos(m_world, m_serverWorld, follow->target.Value(), context.followTargetPosition);

			input = CharacterControlResolver::Resolve(cmd.intent, context, m_controlResolveConfig);
			m_userInputs[control.userId].current = cmd;
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

		m_userInputs.erase(userId);
	}

	void ServerInputSystem::MarkInputApplied(uint64 userId)
	{
		if (userId == 0)
			return;

		auto userIt = m_userInputs.find(userId);
		if (userIt == m_userInputs.end() || !userIt->second.current)
			return;

		ServerUserInputState& inputState = userIt->second;
		if (!inputState.applied || inputState.current->sequence > inputState.applied->sequence)
			inputState.applied = inputState.current;
	}

	uint32 ServerInputSystem::LastAppliedSeq(uint64 userId) const
	{
		if (auto it = m_userInputs.find(userId); it != m_userInputs.end() && it->second.applied)
			return it->second.applied->sequence;
		return 0;
	}

	uint32 ServerInputSystem::LastAppliedControlRevision(uint64 userId) const
	{
		if (auto it = m_userInputs.find(userId); it != m_userInputs.end() && it->second.applied)
			return it->second.applied->intent.controlRevision;
		return 0;
	}

	void ServerInputSystem::QueuePendingInput(uint64 userId, const CharacterControlCommand& cmd)
	{
		if (userId == 0)
			return;
		if (!IsValidIntent(cmd.intent))
			return;

		auto [userIt, insertedUser] = m_userInputs.try_emplace(userId);
		ServerUserInputState& inputState = userIt->second;
		const bool hasInputBaseline = !insertedUser;

		auto& latestRevision = inputState.latestControlRevision;
		if (cmd.intent.controlRevision < latestRevision)
			return;

		uint32 appliedSeq = inputState.applied ? inputState.applied->sequence : 0;
		const bool precedesPending = !inputState.pending.empty()
			&& cmd.sequence < inputState.pending.begin()->first;
		const bool sequenceRestarted = cmd.intent.controlRevision > latestRevision
			&& (cmd.sequence <= appliedSeq || precedesPending);
		if (sequenceRestarted)
		{
			// A ClientWorld recreated for a new membership starts its input sequence
			// from zero. The runtime-wide control revision distinguishes it from late
			// packets belonging to the previous visit to the same WorldId.
			inputState.pending.clear();
			inputState.current.reset();
			inputState.applied.reset();
		}

		if ((!hasInputBaseline || sequenceRestarted) && cmd.sequence > 1)
		{
			// The ClientWorld can tick while membership/player materialization is in
			// progress. Treat the first input that the ready server actor can receive as
			// the membership baseline instead of replaying the unavailable prefix as
			// synthetic Stop commands one slot per simulation tick.
			CharacterControlCommand baseline{};
			baseline.sequence = cmd.sequence - 1;
			inputState.applied = baseline;
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

		auto& pending = inputState.pending;
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

		auto userIt = m_userInputs.find(userId);
		if (userIt == m_userInputs.end())
			return {};

		ServerUserInputState& inputState = userIt->second;
		const uint32 appliedSeq = inputState.applied ? inputState.applied->sequence : 0;
		const uint32 nextSeq = appliedSeq + 1;
		if (!inputState.pending.empty())
		{
			auto& pending = inputState.pending;
			pending.erase(pending.begin(), pending.upper_bound(appliedSeq));

			if (auto commandIt = pending.find(nextSeq); commandIt != pending.end())
			{
				CharacterControlCommand cmd = commandIt->second;
				pending.erase(commandIt);
				return cmd;
			}

			// A newer command proves that the client advanced through nextSeq. Fill
			// the missing unreliable packet with the last continuous state so ACK and
			// client prediction history retain one-step-per-sequence semantics.
			if (!pending.empty() && pending.begin()->first > nextSeq)
			{
				CharacterControlCommand cmd{};
				if (inputState.current)
					cmd = *inputState.current;
				cmd.sequence = nextSeq;
				cmd.intent.edgeActions = 0;
				return cmd;
			}
		}

		if (inputState.current)
		{
			CharacterControlCommand cmd = *inputState.current;
			cmd.intent.edgeActions = 0;
			return cmd;
		}

		return {};
	}
}
