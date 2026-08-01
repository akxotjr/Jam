using UnityEngine;

using JamUnity.Core.Data;
using JamUnity.Authoring.Physics;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.World
{
    [CreateAssetMenu(menuName = "JamUnity/World Archetype Data", fileName = "WorldArchetypeData")]
    public sealed class WorldArchetypeData : AssetData<SharedGen.WorldArchetypeDto>
    {
        [SerializeField] private WorldTemplateData worldTemplate;
        [SerializeField] private PhysicsAssetDatabase physicsAssetDatabase;
        [SerializeField] private string actorLevelName;

        public WorldTemplateData WorldTemplate => worldTemplate;
        public PhysicsAssetDatabase PhysicsAssetDatabase => physicsAssetDatabase;
        public string TemplateName => worldTemplate != null ? worldTemplate.AssetName?.Trim() ?? string.Empty : string.Empty;
        public string ActorLevelName => actorLevelName?.Trim() ?? string.Empty;
        public string PhysicsAssetName => physicsAssetDatabase != null ? physicsAssetDatabase.name?.Trim() ?? string.Empty : string.Empty;

        public override SharedGen.WorldArchetypeDto ToDto()
        {
            return new SharedGen.WorldArchetypeDto
            {
                templateName = TemplateName,
                actorLevelName = ActorLevelName,
                physicsAssetName = PhysicsAssetName,
            };
        }

        public override void FromDto(SharedGen.WorldArchetypeDto dto)
        {
            if (dto == null)
                return;

            actorLevelName = dto.actorLevelName?.Trim() ?? string.Empty;
        }

        public void SetResolvedReferences(
            WorldTemplateData template,
            PhysicsAssetDatabase physicsDatabase)
        {
            worldTemplate = template;
            physicsAssetDatabase = physicsDatabase;
        }

        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }
} // namespace JamUnity.Authoring.World
