#pragma once

#include "jamnet/sync/replication/IReplayRunner.h"

namespace jam::px
{
	class IPhysicsFacade;
}

namespace jam::net
{
	class CorrectionReplayRunner : public IReplayRunner
	{
	public:
		explicit CorrectionReplayRunner(px::IPhysicsFacade* physics);

		void		Prepare(entt::registry& world, const ReplayContext& ctx) override;
		void		Run(entt::registry& world, const ReplayContext& ctx) override;
		void		Commit(entt::registry& world, const ReplayContext& ctx) override;

	private:
		px::IPhysicsFacade*				m_physics			= nullptr;
		std::vector<entt::entity>       m_relevantEntities;
	};


} // namespace jam::net
