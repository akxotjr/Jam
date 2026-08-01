#include "pch.h"
#include "jamnet/runtime/world/simulation/common/CharacterControlResolver.h"

#include <cmath>

namespace jam::net
{
	namespace
	{
		constexpr uint32 kDirectionalMask =
			px::INPUT_FORWARD | px::INPUT_BACKWARD | px::INPUT_LEFT | px::INPUT_RIGHT | px::INPUT_RUN;
	}

	px::CharacterMotorInput CharacterControlResolver::Resolve(
		const CharacterControlIntent& intent,
		const CharacterControlResolveContext& context,
		const CharacterControlResolveConfig& config)
	{
		px::CharacterMotorInput input{};
		const CharacterActionFlags actions = intent.ActionsForTick();
		input.commandEpoch	= intent.controlRevision;
		input.moveReferenceYaw = intent.moveReferenceYaw;
		input.bodyYaw		= context.selfState ? context.selfState->bodyYaw : intent.moveReferenceYaw;
		input.viewYaw		= intent.viewYaw;
		input.viewPitch		= intent.viewPitch;
		if ((actions & static_cast<uint32>(eCharacterActionFlag::Crouch)) != 0)		input.inputFlags |= px::INPUT_CROUCH;
		if ((actions & static_cast<uint32>(eCharacterActionFlag::Prone)) != 0)		input.inputFlags |= px::INPUT_PRONE;
		if ((actions & static_cast<uint32>(eCharacterActionFlag::Jump)) != 0)		input.inputFlags |= px::INPUT_JUMP;
		if ((actions & static_cast<uint32>(eCharacterActionFlag::Dash)) != 0)		input.inputFlags |= px::INPUT_DASH;
		if ((actions & static_cast<uint32>(eCharacterActionFlag::Run)) != 0)		input.inputFlags |= px::INPUT_RUN;
		if ((actions & static_cast<uint32>(eCharacterActionFlag::Sprint)) != 0)		input.inputFlags |= px::INPUT_SPRINT;

		std::visit([&](const auto& locomotion)
			{
				using T = std::decay_t<decltype(locomotion)>;
				if constexpr (std::is_same_v<T, DirectionalMoveIntent>)
				{
					if (locomotion.localX < 0.0f) input.inputFlags |= px::INPUT_LEFT;
					if (locomotion.localX > 0.0f) input.inputFlags |= px::INPUT_RIGHT;
					if (locomotion.localY > 0.0f) input.inputFlags |= px::INPUT_FORWARD;
					if (locomotion.localY < 0.0f) input.inputFlags |= px::INPUT_BACKWARD;
					if (locomotion.localX != 0.0f || locomotion.localY != 0.0f)
					{
						const float yaw = intent.moveReferenceYaw + std::atan2(locomotion.localX, locomotion.localY);
						input.bodyYaw = std::atan2(std::sin(yaw), std::cos(yaw));
					}
				}
				else if constexpr (std::is_same_v<T, MoveToPositionIntent>)
				{
					px::CharacterMotorInput move{};
					move.moveMode		= px::eMoveInputMode::Mouse;
					move.targetPos		= locomotion.target;
					move.inputFlags		= input.inputFlags;
					move.commandEpoch	= input.commandEpoch;
					move.moveReferenceYaw = input.moveReferenceYaw;
					move.bodyYaw		= input.bodyYaw;
					move.viewYaw		= input.viewYaw;
					move.viewPitch		= input.viewPitch;
					
					input = Resolve(move, context, config);
				}
				else if constexpr (std::is_same_v<T, FollowActorIntent>)
				{
					px::CharacterMotorInput move{};
					move.moveMode		= px::eMoveInputMode::Mouse;
					move.mouseMoveKind	= px::eMouseMoveKind::FollowTarget;
					move.targetActorId	= locomotion.target.Value();
					move.inputFlags		= input.inputFlags;
					move.commandEpoch	= input.commandEpoch;
					move.moveReferenceYaw = input.moveReferenceYaw;
					move.bodyYaw		= input.bodyYaw;
					move.viewYaw		= input.viewYaw;
					move.viewPitch		= input.viewPitch;

					input = Resolve(move, context, config);
				}
			}, intent.locomotion);

		if (intent.viewPolicy == eCharacterViewPolicy::FollowMovement)
		{
			input.viewYaw = input.bodyYaw;
			input.viewPitch = 0.0f;
		}
		return input;
	}

	px::CharacterMotorInput CharacterControlResolver::Resolve(
		const px::CharacterMotorInput& source,
		const CharacterControlResolveContext& context,
		const CharacterControlResolveConfig& config)
	{
		if (source.moveMode != px::eMoveInputMode::Mouse)
			return source;

		px::CharacterMotorInput resolved = source;
		resolved.inputFlags = source.inputFlags & ~kDirectionalMask;
		if (!context.selfState)
			return resolved;

		px::Vec3 targetPos = source.targetPos;
		if (source.mouseMoveKind == px::eMouseMoveKind::FollowTarget)
		{
			if (!context.hasFollowTargetPosition)
			{
				if (config.stopWhenFollowTargetUnavailable)
					return resolved;
			}
			else
			{
				targetPos = context.followTargetPosition;
			}
		}

		px::Vec3 toTarget = targetPos - context.selfState->pos;
		toTarget.y = 0.0f;

		const float distanceSq = toTarget.MagnitudeSquared();
		if (distanceSq <= config.stopRadius * config.stopRadius)
		{
			// Point-and-click camera orientation is only a view input. Once the
			// character reaches its target, do not snap its facing back to the
			// camera yaw.
			resolved.bodyYaw = context.selfState->bodyYaw;
			return resolved;
		}

		resolved.bodyYaw = std::atan2(toTarget.x, toTarget.z);
		resolved.moveReferenceYaw = resolved.bodyYaw;
		resolved.inputFlags |= px::INPUT_FORWARD;

		if (distanceSq > config.runDistance * config.runDistance)
			resolved.inputFlags |= px::INPUT_RUN;

		return resolved;
	}
}
