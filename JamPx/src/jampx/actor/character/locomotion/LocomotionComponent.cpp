#include "pch.h"
#include "jampx/actor/character/locomotion/LocomotionComponent.h"
#include "jampx/actor/character/locomotion/DefaultQuakeAccelerator.h"

#include <algorithm>
#include <cmath>
#include <iostream>


namespace jam::px
{

	LocomotionComponent::LocomotionComponent(const CharacterMoveConfig& cfg, PxCapsuleController* controller, PxRigidActor* hitbox)
		: m_cfg(cfg)
	{
		m_accelerator = std::make_unique<DefaultQuakeAccelerator>(cfg);
		m_accumulator = std::make_unique<ExternalMoveAccumulator>();
		m_motor		  = std::make_unique<CharacterMotor>(controller, hitbox);

		m_state.position = m_motor->GetPosition();
		m_state.velocity = Vec3::Zero();
		m_state.grounded = true;
		m_state.air		 = eAirState::Grounded;
	}

	void LocomotionComponent::SetMoveState(const CharacterMoveState& state)
	{
		m_state = state;

		if (m_motor) m_motor->OverrideSense(state.grounded, state.ceiling);
	}

	void LocomotionComponent::Tick(float dt, const MoveIntent& intent)
	{
		if (!m_motor || dt <= 0.0f) return;

		// reset events
		m_state.justLanded = false;
		m_state.justJumped = false;

		m_state.position = m_motor->GetPosition();

		// pre-sense
		const MotorSense sense = m_motor->Sense();
		m_state.grounded	 = sense.grounded;
		m_state.groundNormal = sense.groundNormal;
		m_state.ceiling		 = sense.ceiling;

		UpdateJumpTimers(dt);

		ApplyStanceRequest(intent);
		ApplyGaitRequest(intent);

		auto wish = m_accelerator->BuildWishMovement(intent);
		m_accelerator->Integrate(m_state, wish, dt);

		ApplyJump(intent);
		ApplyGravity(dt);

		ApplyDash(dt, intent);

		if (m_accumulator && m_accumulator->IsOverrideLocomotion())
		{
			const float w = std::clamp(m_accumulator->GetOverrideWeight(), 0.0f, 1.0f);
			m_state.velocity.x *= (1.0f - w);
			m_state.velocity.z *= (1.0f - w);
		}

		Vec3 disp = m_state.velocity * dt;
		
		if (m_accumulator)
		{
			disp += m_accumulator->GetAddDisplacement();
			disp += m_accumulator->GetAddVelocity() * dt;
			m_accumulator->Clear();
		}

		const MotorStepResult step = m_motor->Move(dt, disp);

		m_state.position = step.position;
		// 매 프레임 0으로 리셋되어 중력 누적이 깨지기 때문
		if (dt > 0.f)
		{
			m_state.velocity.x = step.velocity.x;
			m_state.velocity.z = step.velocity.z;
		}

		PostMoveUpdate(step.sense);
	}

	void LocomotionComponent::GetCharacterState(OUT CharacterState& state) const
	{
		if (!m_motor) return;

		state.pos				= m_state.position;
		state.bodyYaw			= m_state.bodyYaw;
		state.moveDir			= HorizontalDir(m_state.velocity);
		state.verticalSpeed		= m_state.velocity.y;
		state.horizontalSpeed	= HorizontalSpeed(m_state.velocity);

		state.stateFlags = 0;
		if (m_state.justJumped || m_state.air == eAirState::Rising)
			SetStateFlag(state.stateFlags, STATE_IS_JUMPING);
		if (m_state.gait == eGait::Sprint)
			SetStateFlag(state.stateFlags, STATE_IS_SPRINT);
	}

	void LocomotionComponent::Teleport(const Vec3& pos)
	{
		if (!m_motor) return;
		m_motor->Teleport(pos);
		m_state.position = pos;
	}

	void LocomotionComponent::UpdateJumpTimers(float dt)
	{
		m_state.jump.coyoteRemain = std::max(0.f, m_state.jump.coyoteRemain - dt);
		m_state.jump.bufferRemain = std::max(0.f, m_state.jump.bufferRemain - dt);

		if (m_state.grounded)
			m_state.jump.coyoteRemain = m_cfg.jump.coyoteTime;
	}

	void LocomotionComponent::ApplyStanceRequest(const MoveIntent& intent)
	{
		const eStance desired = intent.stanceRequest;
		if (desired == m_state.stance)
			return;

		const float desireH = GetStanceHeight(m_cfg.stance, desired);
		if (desired == eStance::Standing && m_state.ceiling)	// stand-up needs space.
			return;	

		if (m_motor->TryResize(desireH))
			m_state.stance = desired;
	}

	void LocomotionComponent::ApplyGaitRequest(const MoveIntent& intent)
	{
		eGait desired = intent.gaitRequest;

		if (m_state.stance == eStance::Prone)
		{
			//todo: prone is basically immobile or crawl. temp: force walk
			desired = eGait::Walk;
		}
		else if (m_state.stance == eStance::Crouching && desired == eGait::Sprint)
		{
			// typical rule: no sprint while crouching
			desired = eGait::Run;
		}

		if (desired == eGait::Sprint && !CanSprint())
			desired = eGait::Run;

		m_state.gait = desired;
	}

	void LocomotionComponent::ApplyJump(const MoveIntent& intent)
	{
		if (intent.jumpPressed)
			m_state.jump.bufferRemain = m_cfg.jump.jumpBuffer;

		const bool buffered = m_state.jump.bufferRemain > 0.f;
		const bool canJump = m_state.grounded || (m_state.jump.coyoteRemain > 0.f);

		if (buffered && canJump)
		{
			m_state.jump.bufferRemain = 0.f;
			m_state.jump.coyoteRemain = 0.f;

			// vertical impulse
			m_state.velocity.y	= m_cfg.jump.speed;
			m_state.grounded	= false;
			m_state.air			= eAirState::Rising;
			m_state.justJumped	= true;
		}
	}

	void LocomotionComponent::ApplyGravity(float dt)
	{
		// only when airbone
		if (!m_state.grounded)
		{
			m_state.velocity.y -= m_cfg.gravity * dt;
		}
		else
		{
			// 지면 밀착(Ground Stick):
			// velocity.y = 0 이면 CCT에 하강 의도가 없어 삼각형 메시 엣지의
			// micro step-up이 매 프레임 누적됨.
			// 소량 음수를 유지해 CCT가 항상 바닥으로 눌리도록 함.
			//m_state.velocity.y = -(m_cfg.gravity * dt);

			constexpr float kGroundStickSpeed = -2.0f;
			m_state.velocity.y = kGroundStickSpeed;
		}
	}

	void LocomotionComponent::ApplyDash(float dt, const MoveIntent& intent)
	{
		if (!m_accumulator) return;

		if (m_state.dash.active)
		{
			m_state.dash.remain = std::max(0.0f, m_state.dash.remain - dt);
			if (m_state.dash.remain <= 0.0f)
				m_state.dash.active = false;
		}

		const bool canStartDash = (!m_state.dash.active) 
							   && (m_cfg.dash.duration > 0.0f) 
							   && (m_cfg.dash.speed > 0.0f)
							   && (m_cfg.dash.allowInAir || m_state.grounded);

		if (intent.dashPressed && canStartDash)
		{
			// moveX=right, moveY=forward (local)
			Vec2 local(intent.moveX, intent.moveY);
			const float axisMag = std::min(1.0f, local.Magnitude());
			local = (axisMag > 0.0001f) ? local.GetNormalized() : Vec2(0.0f, 1.0f);
			const float cy = std::cos(intent.moveYaw);
			const float sy = std::sin(intent.moveYaw);

			const Vec3 forward{ sy, 0.0f, cy };
			const Vec3 right{ cy, 0.0f, -sy };
			Vec3 dir = (forward * local.y + right * local.x).GetNormalized();
			if (dir.MagnitudeSquared() < EPSILON) dir = forward;
			
			m_state.dash.active = true;
			m_state.dash.remain = m_cfg.dash.duration;
			m_state.dash.dir	= dir;
		}

		if (m_state.dash.active)
		{
			// optional steering during dash
			if (m_cfg.dash.steerFactor > 0.0f)
			{
				Vec2 local(intent.moveX, intent.moveY);
				const float axisMag = std::min(1.0f, local.Magnitude());
				if (axisMag > 0.0001f)
				{
					local = local.GetNormalized();
					const float cy = std::cos(intent.moveYaw);
					const float sy = std::sin(intent.moveYaw);
					const Vec3 forward{ sy, 0.0f, cy };
					const Vec3 right{ cy, 0.0f, -sy };
					const Vec3 steerDir = (forward * local.y + right * local.x).GetNormalized();
					const float t = std::clamp(m_cfg.dash.steerFactor, 0.0f, 1.0f);
					
					Vec3 blended = m_state.dash.dir * (1.0f - t) + steerDir * t;
					if (blended.MagnitudeSquared() > 1e-6f)
						m_state.dash.dir = blended.GetNormalized();
				}
			}

			ExternalMoveRequest r{};
			r.layer				 = eExternalMovementLayer::DASH;
			r.addVelocity		 = m_state.dash.dir * m_cfg.dash.speed;
			r.overrideLocomotion = m_cfg.dash.overrideLocomotion;
			r.overrideWeight	 = 1.0f;
			m_accumulator->Add(r);
		}
	}

	void LocomotionComponent::PostMoveUpdate(const MotorSense& sense)
	{
		const bool wasGrounded = m_state.grounded;
		const bool nowGrounded = sense.grounded;

		m_state.grounded	 = nowGrounded;
		m_state.groundNormal = sense.groundNormal;
		m_state.ceiling		 = sense.ceiling;

		if (!wasGrounded && nowGrounded)
		{
			m_state.justLanded = true;
			m_state.air		   = eAirState::Grounded;
			m_state.velocity.y = 0.f;		// 착지 시 수직 속도 클리어
		}
		else if (wasGrounded && !nowGrounded)
		{
			m_state.air = (m_state.velocity.y > 0.f) ? eAirState::Rising : eAirState::Falling;
		}
		else if (!nowGrounded)
		{
			m_state.air = (m_state.velocity.y > 0.f) ? eAirState::Rising : eAirState::Falling;
		}

		if (sense.ceiling)
		{
			m_state.velocity.y = std::min(m_state.velocity.y, 0.f);
		}
	}

	bool LocomotionComponent::CanSprint()
	{
		if (!m_state.grounded && !m_cfg.gait.sprintAllowInAir)
			return false;

		if (m_cfg.gait.sprintMinSpeedToStart > 0.f)
		{
			Vec3 vel = m_state.velocity;
			vel.y = 0.f;
			if (vel.Magnitude() < m_cfg.gait.sprintMinSpeedToStart)
				return false;
		}
		return true;
	}
}
