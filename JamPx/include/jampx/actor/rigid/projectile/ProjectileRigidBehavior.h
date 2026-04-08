#pragma once

#include "jampx/actor/rigid/projectile/ProjectileComponent.h"
#include "jampx/actor/rigid/IRigidBehavior.h"

namespace jam::px
{
	class ProjectileRigidBehavior : public IRigidBehavior
	{
	public:
		explicit ProjectileRigidBehavior(
			std::unique_ptr<ProjectileComponent> mainProjectile,
			std::unique_ptr<ProjectileComponent> replayProjectile);
		~ProjectileRigidBehavior() override = default;

		void							TickOnMain(RigidBody& body, float dt) override;
		void							TickOnReplay(RigidBody& body, float dt) override;

		bool							SyncMainState(RigidBody& body) override;
		void							SyncReplayState(RigidBody& body) override;

		bool							ApplyMainState(const RigidState& state) override;
		bool							ApplyReplayState(const RigidState& state) override;
		
		eActorType						GetActorType() const override { return eActorType::Projectile; }

		void							SetTargetResolver(ProjectileTargetResolver resolver);

		const ProjectileHitResult&		GetLastHitResult() const { return m_lastHitResult; }
		bool							ConsumeMainHitEvent(OUT ProjectileHitResult& result);
		bool							ConsumeMainLifetimeEvent(OUT ProjectileHitResult& result);

	private:
		std::unique_ptr<ProjectileComponent>	m_mainProjectile	= nullptr;
		std::unique_ptr<ProjectileComponent>	m_replayProjectile	= nullptr;

		ProjectileHitResult                     m_lastHitResult		= {};	// from main-projectile
		bool									m_mainHitEventConsumed = false;
		bool									m_mainLifetimeConsumed = false;

		float									m_lastDtMain		= 0.f;
		float									m_lastDtReplay		= 0.f;
	};



} // namespace jam::px
