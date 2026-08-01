using UnityEngine;



namespace JamUnity.Authoring.Physics
{
    [DisallowMultipleComponent]
    [AddComponentMenu("JamUnity/Actor/Physics Archetype Authoring")]
    public sealed class PhysicsArchetypeAuthoring : MonoBehaviour
    {
        [SerializeField] private PhysicsArchetypeData physicsArchetype;
    
        public PhysicsArchetypeData PhysicsArchetype => physicsArchetype;
        public string PhysicsArchetypeName => physicsArchetype != null ? physicsArchetype.AssetName?.Trim() ?? string.Empty : string.Empty;
    
        public void SetPhysicsArchetype(PhysicsArchetypeData value)
        {
            physicsArchetype = value;
        }
    }

} // namespace JamUnity.Authoring.Physics
