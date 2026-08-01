using UnityEngine;

using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Physics
{
    [CreateAssetMenu(menuName = "JamUnity/Physics/Mesh Data", fileName = "PhysicsMeshData")]
    public sealed class MeshData : AssetData<SharedGen.MeshDto>
    {
        [SerializeField] private eMeshType  type;
        [SerializeField] private string     cookedPath      = string.Empty;
        [SerializeField] private string     srcPath         = string.Empty;
        [SerializeField] private int        srcMeshIndex;
        [SerializeField] private int        srcPrimitiveIndex;
    
        public eMeshType Type => type;
        public string CookedPath => cookedPath?.Trim() ?? string.Empty;
    
        public override SharedGen.MeshDto ToDto()
        {
            return new SharedGen.MeshDto
            {
                type                = PhysicsGeneratedDtoAdapter.ToGeneratedMeshType(type),
                cookedPath          = CookedPath,
                srcPath             = srcPath?.Trim() ?? string.Empty,
                srcMeshIndex        = srcMeshIndex,
                srcPrimitiveIndex   = srcPrimitiveIndex,
            };
        }
    
        public override void FromDto(SharedGen.MeshDto dto)
        {
            if (dto == null) return;

            type                = PhysicsGeneratedDtoAdapter.ToRuntimeMeshType(dto.type);
            cookedPath          = dto.cookedPath?.Trim() ?? string.Empty;
            srcPath             = dto.srcPath?.Trim() ?? string.Empty;
            srcMeshIndex        = dto.srcMeshIndex;
            srcPrimitiveIndex   = dto.srcPrimitiveIndex;
        }
    
        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }
    
} // namespace JamUnity.Authoring.Physics
