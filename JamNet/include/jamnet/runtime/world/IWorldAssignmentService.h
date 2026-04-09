#pragma once

#include "jamnet/runtime/world/IWorldAssignmentPolicy.h"
#include "jamnet/runtime/world/WorldAssignmentTypes.h"

#include <memory>

namespace jam::net
{
	class ServerNetworkManager;

	class IWorldAssignmentService
	{
	public:
		virtual ~IWorldAssignmentService() = default;

		virtual void					Init(ServerNetworkManager* owner) = 0;
		virtual void					SetWorldAssignmentPolicy(std::unique_ptr<IWorldAssignmentPolicy> policy) = 0;
		virtual IWorldAssignmentPolicy* GetWorldAssignmentPolicy() const = 0;
		virtual WorldAssignmentResult	AssignPrincipal(const WorldAssignmentRequest& req) = 0;
	};
}
