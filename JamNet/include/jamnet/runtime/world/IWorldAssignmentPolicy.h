#pragma once

#include "jamnet/runtime/world/WorldAssignmentTypes.h"

namespace jam::net
{
	struct WorldAssignmentPolicyRequest
	{
		uint64		principalId		= 0;
		WorldId		currentWorldId	= INVALID_WORLD_ID;
		WorldKey	currentWorld	= INVALID_WORLD_KEY;
	};

	struct WorldAssignmentPolicyResult
	{
		eWorldAssignmentStatus	status				= eWorldAssignmentStatus::Failed;
		WorldId					worldId				= INVALID_WORLD_ID;
		WorldKey				targetWorld			= INVALID_WORLD_KEY;
		WorldOptions			worldOptions		= {};
		eWorldAssignmentAction	preferredAction		= eWorldAssignmentAction::None;
	};

	class ServerNetworkManager;

	class IWorldAssignmentPolicy
	{
	public:
		virtual ~IWorldAssignmentPolicy() = default;

		virtual void Init(ServerNetworkManager* owner) = 0;
		virtual WorldAssignmentPolicyResult EvaluateAssignment(const WorldAssignmentPolicyRequest& req) = 0;
	};
}
