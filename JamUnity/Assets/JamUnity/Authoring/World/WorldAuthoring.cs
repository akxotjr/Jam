using UnityEngine;

using JamUnity.World.Runtime;

namespace JamUnity.Authoring.World
{
    [DisallowMultipleComponent]
    [RequireComponent(typeof(WorldRoot))]
    [AddComponentMenu("JamUnity/World/World Authoring")]
    public sealed class WorldAuthoring : MonoBehaviour
    {
        [SerializeField] private WorldArchetypeData worldArchetype;
        [SerializeField] private WorldRoot worldRoot;

        public WorldArchetypeData WorldArchetype => worldArchetype;
        public WorldRoot WorldRoot => worldRoot;

        public void Configure(WorldArchetypeData archetype, WorldRoot root)
        {
            worldArchetype = archetype;
            worldRoot = root;
        }

        private void OnValidate()
        {
            if (worldRoot == null)
                worldRoot = GetComponent<WorldRoot>();
        }
    }
}
