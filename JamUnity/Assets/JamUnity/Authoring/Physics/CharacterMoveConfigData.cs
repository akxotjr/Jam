using System;
using UnityEngine;

using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Physics
{
    [CreateAssetMenu(menuName = "JamUnity/Physics/Character Move Config Data", fileName = "CharacterMoveConfigData")]
    public sealed class CharacterMoveConfigData : AssetData<SharedGen.CharacterMoveConfigDto>
    {
        [Serializable]
        public struct StanceConfig
        {
            public float standingHeight;
            public float crouchHeight;
            public float crouchSpeedMultiplier;
            public bool  holdToCrouch;
            public float proneHeight;
            public float proneSpeedMultiplier;
            public bool  holdToProne;
        }

        [Serializable]
        public struct GaitConfig
        {
            public float walkSpeedMultiplier;
            public float runSpeedMultiplier;
            public float sprintSpeedMultiplier;
            public float sprintAccelMultiplier;
            public float sprintMinSpeedToStart;
            public bool  sprintAllowInAir;
        }

        [Serializable]
        public struct JumpConfig
        {
            public float speed;
            public float coyoteTime;
            public float jumpBuffer;
            public bool  edgeTrigger;
        }

        [Serializable]
        public struct DashConfig
        {
            public float speed;
            public float duration;
            public bool  overrideLocomotion;
            public bool  allowInAir;
            public bool  endOnCollision;
            public float steerFactor;
        }

        [SerializeField] private float gravity            = 25f;
        [SerializeField] private float groundAccel        = 35f;
        [SerializeField] private float groundFriction     = 8f;
        [SerializeField] private float groundMaxSpeed     = 5f;
        [SerializeField] private float airAccel           = 10f;
        [SerializeField] private float airMaxSpeed        = 10f;
        [SerializeField] private bool  capHorizontalOnly  = true;
        [SerializeField] private float hardSpeedCapAir    = 7.5f;
        [SerializeField] private float softCapStartAir    = 7f;
        [SerializeField] private float softCapStrengthAir = 20f;
        [SerializeField] private StanceConfig stance;
        [SerializeField] private GaitConfig   gait;
        [SerializeField] private JumpConfig   jump;
        [SerializeField] private DashConfig   dash;

        public override SharedGen.CharacterMoveConfigDto ToDto()
        {
            return new SharedGen.CharacterMoveConfigDto
            {
                gravity             = gravity,
                groundAccel         = groundAccel,
                groundFriction      = groundFriction,
                groundMaxSpeed      = groundMaxSpeed,
                airAccel            = airAccel,
                airMaxSpeed         = airMaxSpeed,
                capHorizontalOnly   = capHorizontalOnly,
                hardSpeedCapAir     = hardSpeedCapAir,
                softCapStartAir     = softCapStartAir,
                softCapStrengthAir  = softCapStrengthAir,
                stance              = PhysicsGeneratedDtoAdapter.ToGeneratedStanceDto(stance),
                gait                = PhysicsGeneratedDtoAdapter.ToGeneratedGaitDto(gait),
                jump                = PhysicsGeneratedDtoAdapter.ToGeneratedJumpDto(jump),
                dash                = PhysicsGeneratedDtoAdapter.ToGeneratedDashDto(dash),
            };
        }
        
        public override void FromDto(SharedGen.CharacterMoveConfigDto dto)
        {
            if (dto == null) return;

            gravity             = dto.gravity;
            groundAccel         = dto.groundAccel;
            groundFriction      = dto.groundFriction;
            groundMaxSpeed      = dto.groundMaxSpeed;
            airAccel            = dto.airAccel;
            airMaxSpeed         = dto.airMaxSpeed;
            capHorizontalOnly   = dto.capHorizontalOnly;
            hardSpeedCapAir     = dto.hardSpeedCapAir;
            softCapStartAir     = dto.softCapStartAir;
            softCapStrengthAir  = dto.softCapStrengthAir;
            stance              = PhysicsGeneratedDtoAdapter.ToRuntimeStanceConfig(dto.stance);
            gait                = PhysicsGeneratedDtoAdapter.ToRuntimeGaitConfig(dto.gait);
            jump                = PhysicsGeneratedDtoAdapter.ToRuntimeJumpConfig(dto.jump);
            dash                = PhysicsGeneratedDtoAdapter.ToRuntimeDashConfig(dto.dash);
        }

        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }

} // namespace JamUnity.Authoring.Physics
