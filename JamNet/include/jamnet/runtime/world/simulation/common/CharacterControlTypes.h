#pragma once

#include <jambase/JamTypes.h>

#include "jamnet/runtime/world/actor/ActorId.h"

#include <jampx/PhysicsTypes.h>

#include <variant>

namespace jam::net
{
	struct StopMovementIntent
	{
	};

	struct DirectionalMoveIntent
	{
		float	localX = 0.0f;
		float	localY = 0.0f;
	};

	/// @brief World target resolution 이전의 frontend locomotion request.
	struct MoveByWorldRayIntent
	{
		px::Vec3	rayOrigin	 = px::Vec3::Zero();
		px::Vec3	rayDirection = px::Vec3::Zero();
		float		maxRange	 = 0.0f;
	};

	struct MoveToPositionIntent
	{
		px::Vec3	target = px::Vec3::Zero();
	};

	struct FollowActorIntent
	{
		ActorId		target = ActorId::Invalid();
	};

	using LocomotionIntent = std::variant<
		StopMovementIntent,
		DirectionalMoveIntent,
		MoveByWorldRayIntent,
		MoveToPositionIntent,
		FollowActorIntent>;

	enum class eCharacterActionFlag : uint32
	{
		None	= 0,
		Crouch	= 1 << 0,
		Prone	= 1 << 1,
		Jump	= 1 << 2,
		Dash	= 1 << 3,
		Run		= 1 << 4,
		Sprint	= 1 << 5,
	};

	using CharacterActionFlags = uint32;

	enum class eCharacterViewPolicy : uint8
	{
		FollowMovement,
		Explicit,
	};

	struct CharacterControlIntent
	{
		/// Native ClientCharacterControlCoordinator가 world에 수용한 intent revision.
		/// Frontend sample에는 의미가 없으며 coordinator만 값을 부여한다.
		uint32					controlRevision   = 0;
		float					moveReferenceYaw  = 0.0f;
		float					viewYaw			  = 0.0f;
		float					viewPitch		  = 0.0f;
		eCharacterViewPolicy	viewPolicy		  = eCharacterViewPolicy::FollowMovement;
		CharacterActionFlags	continuousActions = 0;
		CharacterActionFlags	edgeActions		  = 0;
		LocomotionIntent		locomotion		  = StopMovementIntent{};
		CharacterActionFlags ActionsForTick() const
		{
			return continuousActions | edgeActions;
		}
	};

	/// @brief Prediction 및 wire에서 사용할 tick 단위 character control command.
	struct CharacterControlCommand
	{
		uint32					sequence = 0;
		CharacterControlIntent	intent   = {};
	};

	/// @brief Client prediction과 server authority가 공유할 locomotion resolution policy.
	struct CharacterControlResolveConfig
	{
		float	stopRadius  = 1.1f;
		float	runDistance = 10.0f;
		bool	stopWhenFollowTargetUnavailable = true;
	};
}
