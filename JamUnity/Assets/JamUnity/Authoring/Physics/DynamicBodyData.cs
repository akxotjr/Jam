using UnityEngine;

using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Physics
{
    [CreateAssetMenu(menuName = "JamUnity/Physics/Dynamic Body Data", fileName = "DynamicBodyData")]
    public sealed class DynamicBodyData : AssetData<SharedGen.DynamicBodyDto>
    {
        [SerializeField] private float   density        = 1f;
        [SerializeField] private Vector3 linearVelocity;
        [SerializeField] private Vector3 angularVelocity;
        [SerializeField] private float   linearDamping;
        [SerializeField] private float   angularDamping;
    
        public override SharedGen.DynamicBodyDto ToDto()
        {
            return new SharedGen.DynamicBodyDto
            {
                density         = density,
                linearVelocity  = PhysicsGeneratedDtoAdapter.ToList(linearVelocity),
                angularVelocity = PhysicsGeneratedDtoAdapter.ToList(angularVelocity),
                linearDamping   = linearDamping,
                angularDamping  = angularDamping,
            };
        }
    
        public override void FromDto(SharedGen.DynamicBodyDto dto)
        {
            if (dto == null) return;

            density         = dto.density;
            linearVelocity  = PhysicsGeneratedDtoAdapter.ToVector3(dto.linearVelocity);
            angularVelocity = PhysicsGeneratedDtoAdapter.ToVector3(dto.angularVelocity);
            linearDamping   = dto.linearDamping;
            angularDamping  = dto.angularDamping;
        }
    
        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }
    
} // namespace JamUnity.Authoring.Physics  
