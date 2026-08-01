using UnityEngine;

using JamUnity.Core.Data;
using JamUnity.World.Runtime;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.World
{
    [CreateAssetMenu(menuName = "JamUnity/World Template Data", fileName = "WorldTemplateData")]
    public sealed class WorldTemplateData : AssetData<SharedGen.WorldTemplateDto>
    {
        [SerializeField] private uint           group = 1;
        [SerializeField] private bool           allowMultipleInstancePerUser;
        [SerializeField] private bool           persistent;
        [SerializeField] private bool           destroyWhenEmpty = true;
        [SerializeField] private bool           isPrivate;
        [SerializeField] private uint           capacity = 32;

        public uint             Group => group;
        public bool             AllowMultipleInstancePerUser => allowMultipleInstancePerUser;
        public bool             Persistent => persistent;
        public bool             DestroyWhenEmpty => destroyWhenEmpty;
        public bool             IsPrivate => isPrivate;
        public uint             Capacity => capacity;

        public override SharedGen.WorldTemplateDto ToDto()
        {
            return new SharedGen.WorldTemplateDto
            {
                group = (int)group,
                allowMultipleInstancePerUser = allowMultipleInstancePerUser,
                persistent = persistent,
                destroyWhenEmpty = destroyWhenEmpty,
                isPrivate = isPrivate,
                capacity = (int)capacity,
            };
        }

        public override void FromDto(SharedGen.WorldTemplateDto dto)
        {
            if (dto == null)
                return;

            group = dto.group > 0 ? (uint)dto.group : 0u;
            allowMultipleInstancePerUser = dto.allowMultipleInstancePerUser;
            persistent = dto.persistent;
            destroyWhenEmpty = dto.destroyWhenEmpty;
            isPrivate = dto.isPrivate;
            capacity = dto.capacity > 0 ? (uint)dto.capacity : 0u;
        }

        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }

    }
} // namespace JamUnity.Authoring.World
