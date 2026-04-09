#pragma once

#include "jamnet/runtime/world/IWorldAssignmentPolicy.h"

namespace jam::net
{
	class DefaultWorldAssignmentPolicy : public IWorldAssignmentPolicy
	{
	public:
		DefaultWorldAssignmentPolicy() = default;
		~DefaultWorldAssignmentPolicy() override = default;

		void						Init(ServerNetworkManager* manager) override;
		WorldAssignmentPolicyResult EvaluateAssignment(const WorldAssignmentPolicyRequest& req) override;

	private:
		ServerNetworkManager* m_netManager = nullptr;
	};
}
