#pragma once

#include "jampx/actor/rigid/projectile/ProjectileComponent.h"
#include "jampx/actor/rigid/IRigidBehavior.h"

namespace jam::px
{
	class ProjectileRigidBehavior : public IRigidBehavior
	{
	public:
		explicit ProjectileRigidBehavior(std::unique_ptr<ProjectileComponent> projectile);
		~ProjectileRigidBehavior() override = default;

		void		Tick(RigidBody& body, float dt) override;
		eActorType	GetActorType() const override { return eActorType::Projectile; }

		// --- result ---
		const ProjectileHitResult& GetLastHitResult() const { return m_lastHitResult; }
		bool                        IsTerminated()     const { return m_lastHitResult.IsTerminal(); }

	private:
		std::unique_ptr<ProjectileComponent> m_projectile;

		uint16                                  m_teamId = 0;
		ProjectileHitResult                     m_lastHitResult = {};
	};



} // namespace jam::px
