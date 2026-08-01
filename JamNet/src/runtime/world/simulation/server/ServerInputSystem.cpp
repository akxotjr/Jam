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

		const uint32 appliedSeq = LastAppliedSeq(userId);
		if (cmd.sequence <= appliedSeq)
		{
			return;
		}
		if (cmd.intent.controlRevision < LastAppliedControlRevision(userId))
			return;

		auto [it, inserted] = m_pendingInputs.try_emplace(userId, cmd);
		if (inserted)
			return;

		auto& pending = it->second;
		if (cmd.sequence > pending.sequence)
		{
			const CharacterActionFlags accumulatedEdges = pending.intent.edgeActions;
			pending = cmd;
			pending.intent.edgeActions |= accumulatedEdges;
			return;
		}

		// An older command cannot replace the latest continuous state, but its
		// unconsumed edge still belongs to this simulation-tick window.
		if (cmd.sequence < pending.sequence)
			pending.intent.edgeActions |= cmd.intent.edgeActions;
	}

	CharacterControlCommand ServerInputSystem::SelectInputForTick(uint64 userId)
	{
		if (userId == 0)
			return {};

		const uint32 appliedSeq = LastAppliedSeq(userId);
		if (auto pendingIt = m_pendingInputs.find(userId); pendingIt != m_pendingInputs.end())
		{
			CharacterControlCommand cmd = pendingIt->second;
			m_pendingInputs.erase(pendingIt);
			if (cmd.sequence > appliedSeq)
				return cmd;
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
