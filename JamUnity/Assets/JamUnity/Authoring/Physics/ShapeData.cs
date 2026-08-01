using UnityEngine;

using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Physics
{
    [CreateAssetMenu(menuName = "JamUnity/Physics/Shape Data", fileName = "PhysicsShapeData")]
    public sealed class ShapeData : AssetData<SharedGen.ShapeDto>
    {
        [SerializeField] private eShapeType   type;
        [SerializeField] private Vector3      localPosition;
        [SerializeField] private Quaternion   localRotation = Quaternion.identity;
        [SerializeField] private MaterialData material;
        [SerializeField] private eShapeFlag   shapeFlag;
        [SerializeField] private SimulationFilterData simFilter = new();
        [SerializeField] private ShapeQueryFilterData qryFilter = new();
        [SerializeField] private float        contactOffset;
        [SerializeField] private float        restOffset  = 0.02f;
        [SerializeField] private Vector3      halfExtents = Vector3.one * 0.5f;
        [SerializeField] private float        radius      = 0.5f;
        [SerializeField] private float        halfHeight  = 0.5f;
        [SerializeField] private MeshData     mesh;
    
        public eShapeType   Type     => type;
        public MaterialData Material => material;
        public MeshData     Mesh     => mesh;
    
        public override SharedGen.ShapeDto ToDto()
        {
            return new SharedGen.ShapeDto
            {
                type            = PhysicsGeneratedDtoAdapter.ToGeneratedShapeType(type),
                localPose       = PhysicsGeneratedDtoAdapter.ToTransformDto(localPosition, localRotation),
                material        = PhysicsGeneratedDtoAdapter.ToHandleName(material),
                shapeFlag       = PhysicsGeneratedDtoAdapter.ToGeneratedShapeFlag(shapeFlag),
                simFilter       = PhysicsGeneratedDtoAdapter.ToSimFilterDto(simFilter),
                qryFilter       = PhysicsGeneratedDtoAdapter.ToQueryFilterDto(qryFilter),
                contactOffset   = contactOffset,
                restOffset      = restOffset,
                halfExtents     = PhysicsGeneratedDtoAdapter.ToList(halfExtents),
                radius          = radius,
                halfHeight      = halfHeight,
                mesh            = PhysicsGeneratedDtoAdapter.ToHandleName(mesh),
            };
        }
    
        public override void FromDto(SharedGen.ShapeDto dto)
        {
            if (dto == null) return;

            type            = PhysicsGeneratedDtoAdapter.ToRuntimeShapeType(dto.type);
            localPosition   = PhysicsGeneratedDtoAdapter.ToVector3(dto.localPose?.p);
            localRotation   = PhysicsGeneratedDtoAdapter.ToQuaternion(dto.localPose?.q);
            shapeFlag       = PhysicsGeneratedDtoAdapter.ToRuntimeShapeFlag(dto.shapeFlag);
            simFilter       = PhysicsGeneratedDtoAdapter.ToSimulationFilterData(dto.simFilter);
            qryFilter       = PhysicsGeneratedDtoAdapter.ToShapeQueryFilterData(dto.qryFilter);
            contactOffset   = dto.contactOffset;
            restOffset      = dto.restOffset;
            halfExtents     = PhysicsGeneratedDtoAdapter.ToVector3(dto.halfExtents);
            radius          = dto.radius;
            halfHeight      = dto.halfHeight;
        }
    
        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }
    
} // namespace JamUnity.Authoring.Physics
