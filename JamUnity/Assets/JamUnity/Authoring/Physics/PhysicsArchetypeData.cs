using System.Collections.Generic;
using UnityEngine;

using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Physics
{
    [CreateAssetMenu(menuName = "JamUnity/Physics/Physics Archetype Data", fileName = "PhysicsArchetypeData")]
    public sealed class PhysicsArchetypeData : AssetData<SharedGen.PhysicsArchetypeDto>
    {
        [SerializeField] private eActorType                actorType;
        [SerializeField] private eBodyType                 bodyType;
        [SerializeField] private eMotionType               motionType;
        [SerializeField] private eMotionFlag               motionFlags;
        [SerializeField] private List<ShapeData>           rigidShapes = new();
        [SerializeField] private DynamicBodyData           dynamicBody;
        [SerializeField] private eRigidBehaviorKind        behaviorKind;
        [SerializeField] private KinematicDriverConfigData kinematicBehavior;
        [SerializeField] private ProjectileConfigData      projectileBehavior;
        [SerializeField] private CctBodyData               cctBody;
        [SerializeField] private List<ShapeData>           hitboxes = new();
        [SerializeField] private eControllerType           controllerType = eControllerType.Player;
        [SerializeField] private CharacterMoveConfigData   moveConfig;
    
        public eActorType ActorType => actorType;
        public eBodyType BodyType => bodyType;
        public eMotionType MotionType => motionType;
        public eRigidBehaviorKind BehaviorKind => behaviorKind;
        public IReadOnlyList<ShapeData> RigidShapes => rigidShapes;
        public DynamicBodyData DynamicBody => dynamicBody;
        public KinematicDriverConfigData KinematicBehavior => kinematicBehavior;
        public ProjectileConfigData ProjectileBehavior => projectileBehavior;
        public CctBodyData CctBody => cctBody;
        public IReadOnlyList<ShapeData> Hitboxes => hitboxes;
        public CharacterMoveConfigData MoveConfig => moveConfig;
        
        public override SharedGen.PhysicsArchetypeDto ToDto()
        {
            if (bodyType == eBodyType.RigidBody)
            {
                var shapes = new List<string>(rigidShapes.Count);
                for (int i = 0; i < rigidShapes.Count; ++i)
                    shapes.Add(PhysicsGeneratedDtoAdapter.ToHandleName(rigidShapes[i]));

                SharedGen.RigidBehaviorDto behavior = null;
                if (behaviorKind == eRigidBehaviorKind.KinematicDriver && kinematicBehavior != null)
                {
                    behavior = new SharedGen.RigidBehaviorDto
                    {
                        kind = PhysicsGeneratedDtoAdapter.ToGeneratedRigidBehaviorKind(behaviorKind),
                        config = PhysicsGeneratedDtoAdapter.ToHandleName(kinematicBehavior),
                    };
                }
                else if (behaviorKind == eRigidBehaviorKind.Projectile && projectileBehavior != null)
                {
                    behavior = new SharedGen.RigidBehaviorDto
                    {
                        kind = PhysicsGeneratedDtoAdapter.ToGeneratedRigidBehaviorKind(behaviorKind),
                        config = PhysicsGeneratedDtoAdapter.ToHandleName(projectileBehavior),
                    };
                }

                return new SharedGen.RigidPhysicsArchetypeDto
                {
                    actorType        = PhysicsGeneratedDtoAdapter.ToGeneratedRigidActorType(actorType),
                    motionFlags      = PhysicsGeneratedDtoAdapter.ToGeneratedRigidMotionFlags(motionFlags),
                    motionType       = PhysicsGeneratedDtoAdapter.ToGeneratedRigidMotionType(motionType),
                    bodyType         = PhysicsGeneratedDtoAdapter.ToBodyTypeText(bodyType),
                    body = new SharedGen.RigidBodyDto
                    {
                        shapes   = shapes,
                        dynamic  = PhysicsGeneratedDtoAdapter.ToHandleName(dynamicBody),
                        behavior = behavior,
                    }
                };
            }

            var hitboxNames = new List<string>(hitboxes.Count);
            for (int i = 0; i < hitboxes.Count; ++i)
                hitboxNames.Add(PhysicsGeneratedDtoAdapter.ToHandleName(hitboxes[i]));

            return new SharedGen.CharacterPhysicsArchetypeDto
            {
                actorType        = PhysicsGeneratedDtoAdapter.ToGeneratedCharacterActorType(actorType),
                motionFlags      = PhysicsGeneratedDtoAdapter.ToGeneratedCharacterMotionFlags(motionFlags),
                motionType       = PhysicsGeneratedDtoAdapter.ToGeneratedCharacterMotionType(motionType),
                bodyType         = PhysicsGeneratedDtoAdapter.ToBodyTypeText(bodyType),
                body = new SharedGen.CharacterBodyDto
                {
                    cct            = PhysicsGeneratedDtoAdapter.ToHandleName(cctBody),
                    hitboxes       = hitboxNames,
                    controllerType = PhysicsGeneratedDtoAdapter.ToGeneratedControllerType(controllerType),
                    moveConfig     = PhysicsGeneratedDtoAdapter.ToHandleName(moveConfig),
                }
            };
        }
        
        public override void FromDto(SharedGen.PhysicsArchetypeDto dto)
        {
            if (dto == null)
                return;

            switch (dto)
            {
                case SharedGen.RigidPhysicsArchetypeDto rigid:
                    actorType        = PhysicsGeneratedDtoAdapter.ToRuntimeActorType(rigid.actorType);
                    bodyType         = eBodyType.RigidBody;
                    motionType       = PhysicsGeneratedDtoAdapter.ToRuntimeMotionType(rigid.motionType);
                    motionFlags      = PhysicsGeneratedDtoAdapter.ToRuntimeMotionFlags(rigid.motionFlags);
                    behaviorKind     = PhysicsGeneratedDtoAdapter.ToRuntimeRigidBehaviorKind(rigid.body.behavior.kind);
                    break;

                case SharedGen.CharacterPhysicsArchetypeDto character:
                    actorType        = PhysicsGeneratedDtoAdapter.ToRuntimeActorType(character.actorType);
                    bodyType         = eBodyType.CharacterBody;
                    motionType       = PhysicsGeneratedDtoAdapter.ToRuntimeMotionType(character.motionType);
                    motionFlags      = PhysicsGeneratedDtoAdapter.ToRuntimeMotionFlags(character.motionFlags);
                    controllerType   = PhysicsGeneratedDtoAdapter.ToRuntimeControllerType(character.body.controllerType);
                    behaviorKind     = eRigidBehaviorKind.None;
                    break;
            }
        }
    
        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }
    
} // namespace JamUnity.Authoring.Physics
