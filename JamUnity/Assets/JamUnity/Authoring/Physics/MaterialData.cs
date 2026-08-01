using UnityEngine;

using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Physics
{
    [CreateAssetMenu(menuName = "JamUnity/Physics/Material Data", fileName = "PhysicsMaterialData")]
    public sealed class MaterialData : AssetData<SharedGen.MaterialDto>
    {
        [SerializeField] private float staticFriction  = 0.5f;
        [SerializeField] private float dynamicFriction = 0.5f;
        [SerializeField] private float restitution     = 0.1f;
    
        public float StaticFriction  => staticFriction;
        public float DynamicFriction => dynamicFriction;
        public float Restitution     => restitution;
    
        public override SharedGen.MaterialDto ToDto()
        {
            return new SharedGen.MaterialDto
            {
                staticFriction  = staticFriction,
                dynamicFriction = dynamicFriction,
                restitution     = restitution,
            };
        }
        
        public override void FromDto(SharedGen.MaterialDto dto)
        {
            if (dto == null)
                return;

            staticFriction  = dto.staticFriction;
            dynamicFriction = dto.dynamicFriction;
            restitution     = dto.restitution;
        }
    
        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }

} // namespace JamUnity.Authoring.Physics
