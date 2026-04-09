#include "pch.h"
#include "jamnet/runtime/world/DefaultWorldAssignmentPolicy.h"

namespace jam::net
{
	void DefaultWorldAssignmentPolicy::Init(ServerNetworkManager* manager)
	{
		m_netManager = manager;
	}

	WorldAssignmentPolicyResult DefaultWorldAssignmentPolicy::EvaluateAssignment(const WorldAssignmentPolicyRequest& req)
	{
		if (!m_netManager || req.principalId == 0)
			return {};

		WorldAssignmentPolicyResult result{};
		result.status			= eWorldAssignmentStatus::Assigned;
		result.targetWorld		= WorldKey::FromSharedWorldTemplate(1);
		result.preferredAction	= (req.currentWorldId != INVALID_WORLD_ID) ? eWorldAssignmentAction::Transfer : eWorldAssignmentAction::None;

		return result;
	}
}
