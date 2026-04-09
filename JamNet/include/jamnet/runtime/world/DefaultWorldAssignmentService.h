#pragma once

#include "jamnet/runtime/world/IWorldAssignmentService.h"

namespace jam::net
{
	class DefaultWorldAssignmentService : public IWorldAssignmentService
	{
	public:
		DefaultWorldAssignmentService();
		~DefaultWorldAssignmentService() override = default;

		void						Init(ServerNetworkManager* owner) override;
		void						SetWorldAssignmentPolicy(std::unique_ptr<IWorldAssignmentPolicy> policy) override;
		IWorldAssignmentPolicy*		GetWorldAssignmentPolicy() const override { return m_policy.get(); }
		WorldAssignmentResult		AssignPrincipal(const WorldAssignmentRequest& req) override;

	private:
		WorldAssignmentDecision		BuildDecision(const WorldAssignmentRequest& req) const;

	private:
		ServerNetworkManager*					m_netManager = nullptr;
		std::unique_ptr<IWorldAssignmentPolicy> m_policy	 = nullptr;
	};
}
