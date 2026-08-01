using UnityEngine;

using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Physics
{
    [CreateAssetMenu(menuName = "JamUnity/Physics/Projectile Config Data", fileName = "ProjectileConfigData")]
    public sealed class ProjectileConfigData : AssetData<SharedGen.ProjectileConfigDto>
    {
        [SerializeField] private eProjectileKind        kind;
        [SerializeField] private eProjectileMotionModel motionModel;
        [SerializeField] private Vector3                initialVelocity;
        [SerializeField] private float                  gravityScale    = 1f;
        [SerializeField] private eProjectileHitModel    hitModel;
        [SerializeField] private bool                   useShapeSweep   = true;
        [SerializeField] private bool                   fallbackRaycast = true;
        [SerializeField] private FilterData             requestFd;
        [SerializeField] private float                  maxRange        = 100f;
        [SerializeField] private float                  maxLifetime     = 5f;

        public override SharedGen.ProjectileConfigDto ToDto()
        {
            return new SharedGen.ProjectileConfigDto
            {
                kind = PhysicsGeneratedDtoAdapter.ToGeneratedProjectileKind(kind),
                motion = new SharedGen.ProjectileMotionConfigDto
                {
                    model           = PhysicsGeneratedDtoAdapter.ToGeneratedProjectileMotionModel(motionModel),
                    initialVelocity = PhysicsGeneratedDtoAdapter.ToList(initialVelocity),
                    gravityScale    = gravityScale,
                },
                hit = new SharedGen.ProjectileHitConfigDto
                {
                    model           = PhysicsGeneratedDtoAdapter.ToGeneratedProjectileHitModel(hitModel),
                    useShapeSweep   = useShapeSweep,
                    fallbackRaycast = fallbackRaycast,
                    requestFd       = PhysicsGeneratedDtoAdapter.ToQueryFilterDto(requestFd),
                },
                lifetime = new SharedGen.ProjectileLifetimeConfigDto
                {
                    maxRange    = maxRange,
                    maxLifetime = maxLifetime,
                },
            };
        }

        public override void FromDto(SharedGen.ProjectileConfigDto dto)
        {
            if (dto == null) return;

            kind = PhysicsGeneratedDtoAdapter.ToRuntimeProjectileKind(dto.kind);
            if (dto.motion != null)
            {
                motionModel     = PhysicsGeneratedDtoAdapter.ToRuntimeProjectileMotionModel(dto.motion.model);
                gravityScale    = dto.motion.gravityScale;
                initialVelocity = PhysicsGeneratedDtoAdapter.ToVector3(dto.motion.initialVelocity);
            }

            if (dto.hit != null)
            {
                hitModel        = PhysicsGeneratedDtoAdapter.ToRuntimeProjectileHitModel(dto.hit.model);
                useShapeSweep   = dto.hit.useShapeSweep;
                fallbackRaycast = dto.hit.fallbackRaycast;
                requestFd       = PhysicsGeneratedDtoAdapter.ToFilterData(dto.hit.requestFd);
            }

            if (dto.lifetime != null)
            {
                maxRange    = dto.lifetime.maxRange;
                maxLifetime = dto.lifetime.maxLifetime;
            }
        }

        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }
} // namespace JamUnity.Authoring.Physics
