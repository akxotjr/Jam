#pragma once

#include "jamnet/runtime/world/simulation/common/IReplayRunner.h"

namespace jam::px
{
	class PhysicsFacade;
}

namespace jam::net
{
	class CorrectionReplayRunner : public IReplayRunner
	{
	public:
		explicit CorrectionReplayRunner(px::PhysicsFacade* physics);

		void		Prepare(entt::registry& world, const ReplayContext& ctx) override;
		void		Run(entt::registry& world, const ReplayContext& ctx) override;
		void		Commit(entt::registry& world, const ReplayContext& ctx) override;

	private:
		px::PhysicsFacade*				m_physics			= nullptr;
		std::vector<entt::entity>       m_relevantEntities;
	};


} // namespace jam::net
