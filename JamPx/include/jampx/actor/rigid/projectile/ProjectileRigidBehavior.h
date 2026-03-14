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

		void						Tick(RigidBody& body, float dt) override;
		void						SyncState(RigidBody& body) override;
		eActorType					GetActorType() const override { return eActorType::Projectile; }

		void						SetTargetResolver(ProjectileTargetResolver resolver);

		// --- result ---
		const ProjectileHitResult&	GetLastHitResult() const { return m_lastHitResult; }
		bool                        IsTerminated()     const { return m_lastHitResult.IsTerminal(); }

	private:
		std::unique_ptr<ProjectileComponent>	m_projectile    = nullptr;
		ProjectileHitResult                     m_lastHitResult = {};
		float									m_lastDt = 0.f;
	};



} // namespace jam::px
