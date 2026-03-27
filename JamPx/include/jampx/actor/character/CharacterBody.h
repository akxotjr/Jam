#pragma once

#include "jampx/actor/character/locomotion/LocomotionComponent.h"
#include "jampx/actor/character/controller/ICharacterController.h"

namespace jam::px
{
	enum class ePxSceneSlot : uint8;

	class CharacterBody
	{
	public:
		CharacterBody(
			PxCapsuleController* mainCCT, 
			PxCapsuleController* replayCCT, 
			PxRigidActor* hitbox, 
			const CharacterMoveConfig& cfg);


		// --- brain (Player / AI) ---
		void                            SetBrain(std::unique_ptr<ICharacterController> brain);
		ICharacterController*			GetBrain() const { return m_brain.get(); }

		// --- tick ---
		bool                            TickOnMain(float dt);
		void							TickOnReplay(float dt);

		// --- state / config ---
		const CharacterState&			GetMainState() const { return m_mainState; }
		const CharacterState&			GetReplayState() const { return m_replayState; }

		void                            SetMainState(const CharacterState& s) { m_mainState = s; }
		void							SetReplayState(const CharacterState& s) { m_replayState = s; }

		void                            ApplyAuthorityToMain(const CharacterState& s);
		void                            ApplyAuthorityToReplay(const CharacterState& s);
		void                            ApplyAuthorityToBoth(const CharacterState& s);

		void							SetPlayerInput(const CharacterInput& input);
		void                            SetFacing(float yaw, float pitch);

		const CharacterMoveConfig&		GetConfig() const;
		void                            SetConfig(const CharacterMoveConfig& cfg);


		PxCapsuleController*			GetController(ePxSceneSlot slot = ePxSceneSlot::Main) const;
		PxCapsuleController*			GetMainController()   const { return m_mainCCT; }
		PxCapsuleController*			GetReplayController() const { return m_replayCCT; }
		bool							HasMainController()   const { return m_mainCCT != nullptr; }
		bool                            HasReplayController() const { return m_replayCCT != nullptr; }
		
		PxRigidActor*					GetHitbox() const { return m_hitbox; }
		LocomotionComponent*			GetMover(ePxSceneSlot slot = ePxSceneSlot::Main) const;
		LocomotionComponent*			GetMainMover()   const { return m_mainMover.get(); }
		LocomotionComponent*			GetReplayMover() const { return m_replayMover.get(); }

		eBodyType                       GetBodyType() const { return eBodyType::Character; }


	private:
		void							RebuildMoveStateFromAuthority(LocomotionComponent* mover, const CharacterState& s);


	private:
		PxCapsuleController*					m_mainCCT		= nullptr;  // Main scene controller
		PxCapsuleController*					m_replayCCT		= nullptr;  // Replay scene controller (for correction)

		PxRigidActor*							m_hitbox		= nullptr;

		CharacterState                          m_mainState		= {};
		CharacterState							m_replayState	= {};

		std::unique_ptr<ICharacterController>   m_brain			= nullptr;

		std::unique_ptr<LocomotionComponent>    m_mainMover		= nullptr;
		std::unique_ptr<LocomotionComponent>	m_replayMover	= nullptr;
	};
}