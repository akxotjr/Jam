using UnityEngine;

using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Physics
{
    [CreateAssetMenu(menuName = "JamUnity/Physics/Character Controller Body Data", fileName = "CctBodyData")]
    public sealed class CctBodyData : AssetData<SharedGen.CctBodyDto>
    {
        [SerializeField] private float          radius          = 0.5f;
        [SerializeField] private float          height          = 1f;
        [SerializeField] private MaterialData   material;
        [SerializeField] private float          density         = 10f;
        [SerializeField] private eCCTPolicy     policy          = eCCTPolicy.Default;
        [SerializeField] private SimulationFilterData simFilter = new()
        {
            category = eSimulationCategory.Character,
            mask = eSimulationCategory.WorldStatic |
                   eSimulationCategory.WorldDynamic |
                   eSimulationCategory.Sensor,
        };
        [SerializeField] private ShapeQueryFilterData qryFilter = new()
        {
            category = eQueryCategory.Character,
        };
        [SerializeField] private float          slopeLimit      = 0.707f;
        [SerializeField] private float          invisibleWallHeight;
        [SerializeField] private float          maxJumpHeight;
        [SerializeField] private float          contactOffset   = 0.1f;
        [SerializeField] private float          stepOffset      = 0.5f;
        [SerializeField] private float          scaleCoeff      = 0.8f;
        [SerializeField] private float          volumeGrowth    = 1.5f;

        public MaterialData Material => material;

        public override SharedGen.CctBodyDto ToDto()
        {
            return new SharedGen.CctBodyDto
            {
                radius              = radius,   
                height              = height,
                material            = PhysicsGeneratedDtoAdapter.ToHandleName(material),
                density             = density,
                policy              = PhysicsGeneratedDtoAdapter.ToGeneratedCctPolicy(policy),
                simFilter           = PhysicsGeneratedDtoAdapter.ToSimFilterDto(simFilter),
                qryFilter           = PhysicsGeneratedDtoAdapter.ToQueryFilterDto(qryFilter),
                slopeLimit          = slopeLimit,
                invisibleWallHeight = invisibleWallHeight,
                maxJumpHeight       = maxJumpHeight,
                contactOffset       = contactOffset,
                stepOffset          = stepOffset,
                scaleCoeff          = scaleCoeff,
                volumeGrowth        = volumeGrowth,
            };
        }
        
        public override void FromDto(SharedGen.CctBodyDto dto)
        {
            if (dto == null) return;

            radius              = dto.radius;
            height              = dto.height;
            density             = dto.density;
            policy              = PhysicsGeneratedDtoAdapter.ToRuntimeCctPolicy(dto.policy);
            simFilter           = PhysicsGeneratedDtoAdapter.ToSimulationFilterData(dto.simFilter);
            qryFilter           = PhysicsGeneratedDtoAdapter.ToShapeQueryFilterData(dto.qryFilter);
            slopeLimit          = dto.slopeLimit;
            invisibleWallHeight = dto.invisibleWallHeight;
            maxJumpHeight       = dto.maxJumpHeight;
            contactOffset       = dto.contactOffset;
            stepOffset          = dto.stepOffset;
            scaleCoeff          = dto.scaleCoeff;
            volumeGrowth        = dto.volumeGrowth;
        }

        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }

} // namespace JamUnity.Authoring.Physics
