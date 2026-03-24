#include "pch.h"
#include "jampx/actor/character/CharacterBody.h"
#include "jampx/actor/character/controller/PlayerControllerComponent.h"

namespace jam::px
{

	CharacterBody::CharacterBody(PxCapsuleController* mainCCT, PxCapsuleController* replayCCT, PxRigidActor* hitbox, const CharacterMoveConfig& cfg)
		: m_mainCCT(mainCCT),
		  m_replayCCT(replayCCT),
		  m_hitbox(hitbox), 
		  m_mainMover(std::make_unique<LocomotionComponent>(cfg, mainCCT, hitbox)),
		  m_replayMover(std::make_unique<LocomotionComponent>(cfg, replayCCT, nullptr))
	{
		if (m_mainMover)
			m_mainState.pos = m_mainMover->GetMoveState().position;
		if (m_replayMover)
			m_replayState.pos = m_replayMover->GetMoveState().position;
	}

	void CharacterBody::SetBrain(std::unique_ptr<ICharacterController> brain)
	{
		m_brain = std::move(brain);
	}

	void CharacterBody::TickOnMain(float dt)
	{
		const MoveIntent intent = m_brain ? m_brain->BuildIntent(dt) : MoveIntent{};

		if (!m_mainMover) return;
		m_mainMover->Tick(dt, intent);

		const float savedPitch = m_mainState.facingPitch;
		m_mainMover->GetCharacterState(m_mainState);
		m_mainState.facingPitch = savedPitch;
	}

	void CharacterBody::TickOnReplay(float dt)
	{
		const MoveIntent intent = m_brain ? m_brain->BuildIntent(dt) : MoveIntent{};
		if (!m_replayMover) return;

		m_replayMover->Tick(dt, intent);

		const float savedPitch = m_replayState.facingPitch;
		m_replayMover->GetCharacterState(m_replayState);
		m_replayState.facingPitch = savedPitch;
	}


	void CharacterBody::ApplyAuthorityToMain(const CharacterState& s)
	{
		m_mainState = s;
		RebuildMoveStateFromAuthority(m_mainMover.get(), s);
	}

	void CharacterBody::ApplyAuthorityToReplay(const CharacterState& s)
	{
		m_replayState = s;
		RebuildMoveStateFromAuthority(m_replayMover.get(), s);
	}

	void CharacterBody::ApplyAuthorityToBoth(const CharacterState& s)
	{
		ApplyAuthorityToMain(s);
		ApplyAuthorityToReplay(s);
	}

	void CharacterBody::SetPlayerInput(const CharacterInput& input)
	{
		if (auto* brain = dynamic_cast<PlayerControllerComponent*>(m_brain.get()))
			brain->SetInput(input);

		SetFacing(input.facingYaw, input.facingPitch);
	}

	void CharacterBody::SetFacing(float yaw, float pitch)
	{
		m_mainState.facingYaw   = yaw;
		m_mainState.facingPitch = pitch;
	}

	const CharacterMoveConfig& CharacterBody::GetConfig() const
	{
		return m_mainMover->GetConfig();
	}

	void CharacterBody::SetConfig(const CharacterMoveConfig& cfg)
	{
		if (m_mainMover)   m_mainMover->SetConfig(cfg);
		if (m_replayMover) m_replayMover->SetConfig(cfg);
	}

	PxCapsuleController* CharacterBody::GetController(ePxSceneSlot slot) const
	{
		return (slot == ePxSceneSlot::Replay) ? m_replayCCT : m_mainCCT;
	}

	LocomotionComponent* CharacterBody::GetMover(ePxSceneSlot slot) const
	{
		return (slot == ePxSceneSlot::Replay) ? m_replayMover.get() : m_mainMover.get();
	}





	void CharacterBody::RebuildMoveStateFromAuthority(LocomotionComponent* mover, const CharacterState& s)
	{
		if (!mover) return;

		mover->Teleport(s.pos);

		CharacterMoveState mv = mover->GetMoveState();
		mv.position = s.pos;
		mv.bodyYaw  = s.facingYaw;

		const float hSpd = s.horizontalSpeed;
		mv.velocity.x = s.moveDir.x * hSpd;
		mv.velocity.z = s.moveDir.y * hSpd;
		mv.velocity.y = s.verticalSpeed;

		const bool isJumping = HasStateFlag(s.stateFlags, STATE_IS_JUMPING);
		if (isJumping || s.verticalSpeed > 0.01f)
		{
			mv.grounded = false;
			mv.air = s.verticalSpeed > 0.f ? eAirState::Rising : eAirState::Falling;
		}
		else if (s.verticalSpeed < -0.01f)
		{
			mv.grounded = false;
			mv.air = eAirState::Falling;
		}
		else
		{
			mv.grounded = true;
			mv.air = eAirState::Grounded;
		}

		mv.jump.coyoteRemain = 0.f;
		mv.jump.bufferRemain = 0.f;

		mover->SetMoveState(mv);
	}


} // namespace jam::px
