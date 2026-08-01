using UnityEngine;

using JamUnity.Core.Data;
using JamUnity.Authoring.Physics;

using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Actor
{
    public enum eActorSpawnPolicy
    {
        LevelOnly,
        RuntimeOnly,
        Both,
    }

    [CreateAssetMenu(menuName = "JamUnity/Actor Archetype Data", fileName = "ActorArchetypeData")]
    public sealed class ActorArchetypeData : AssetData<SharedGen.ActorArchetypeDto>
    {
        [SerializeField] 
        private PhysicsArchetypeData physicsArchetype;
        [SerializeField] private eActorSpawnPolicy spawnPolicy = eActorSpawnPolicy.Both;
        [SerializeField] private bool allowReplication = true;

        public string ArchetypeName => AssetName;
        public PhysicsArchetypeData PhysicsArchetype => physicsArchetype;
        public eActorSpawnPolicy SpawnPolicy => spawnPolicy;
        public bool AllowReplication => allowReplication;

        public override SharedGen.ActorArchetypeDto ToDto()
        {
            string archetypeName = AssetName;
            if (string.IsNullOrWhiteSpace(archetypeName))
                return null;

            return new SharedGen.ActorArchetypeDto
            {
                physicsArchetype = physicsArchetype != null ? physicsArchetype.AssetName?.Trim() ?? string.Empty : string.Empty,
                spawnPolicy = (SharedGen.eActorArchetypeDtoSpawnPolicy)(int)spawnPolicy,
                allowReplication = allowReplication,
            };
        }

        public override void FromDto(SharedGen.ActorArchetypeDto dto)
        {
            if (dto == null)
                return;
            spawnPolicy = (eActorSpawnPolicy)(int)dto.spawnPolicy;
            allowReplication = dto.allowReplication;
        }

        public void SetArchetypeName(string value)
        {
            AssetName = value;
        }

        public void SetPhysicsArchetype(PhysicsArchetypeData value)
        {
            physicsArchetype = value;
        }

        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }

} // namespace JamUnity.Authoring.Actor
