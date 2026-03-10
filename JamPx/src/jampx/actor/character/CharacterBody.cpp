#include "pch.h"
#include "jampx/actor/character/CharacterBody.h"


namespace jam::px
{
	CharacterBody::CharacterBody(PxCapsuleController* controller, PxRigidActor* hitbox, const CharacterMoveConfig& cfg)
		: m_controller(controller), m_hitbox(hitbox), m_mover(std::make_unique<LocomotionComponent>(cfg, controller, hitbox))
	{
		m_state.pos = m_mover->GetMoveState().position;
	}

	void CharacterBody::SetBrain(std::unique_ptr<ICharacterController> brain)
	{
		m_brain = std::move(brain);
	}

	void CharacterBody::Tick(float dt)
	{
		const MoveIntent intent = m_brain ? m_brain->BuildIntent(dt) : MoveIntent{};

		if (!m_mover) return;
		m_mover->Tick(dt, intent);

		// locomotion 결과를 m_state 로 동기화 (yaw, speed, flags 등)
		// facingPitch 는 locomotion 미관여 — SetFacing() 을 통해 별도 유지
		const float savedPitch = m_state.facingPitch;
		m_mover->GetCharacterState(m_state);
		m_state.facingPitch = savedPitch;
	}

	void CharacterBody::SetState(const CharacterState& s)
	{
		m_state = s;

		if (!m_mover) return;

		// 1. CCT 위치 복원 (sense 캐시 무효화 포함)
		m_mover->Teleport(s.pos);

		// 2. 내부 MoveState 재구성
		CharacterMoveState mv = m_mover->GetMoveState();

		mv.position = s.pos;
		mv.bodyYaw = s.facingYaw;

		// velocity 재구성: moveDir(XZ) * horizontalSpeed + verticalSpeed(Y)
		const float hSpd = s.horizontalSpeed;
		mv.velocity.x = s.moveDir.x * hSpd;
		mv.velocity.z = s.moveDir.y * hSpd;
		mv.velocity.y = s.verticalSpeed;

		// air 상태 복원
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

		m_mover->SetMoveState(mv);
	}


	void CharacterBody::SetFacing(float yaw, float pitch)
	{
		m_state.facingYaw = yaw;
		m_state.facingPitch = pitch;
	}

	const CharacterMoveConfig& CharacterBody::GetConfig() const
	{
		return m_mover->GetConfig();
	}

	void CharacterBody::SetConfig(const CharacterMoveConfig& cfg)
	{
		if (m_mover) m_mover->SetConfig(cfg);
	}

	PhysicsHandle CharacterBody::GetPhysicsHandle() const
	{
		return PhysicsHandle{ reinterpret_cast<uint64_t>(m_controller) };
	}


} // namespace jam::px
