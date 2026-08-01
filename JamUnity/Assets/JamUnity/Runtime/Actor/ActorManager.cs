using UnityEngine;
using ActorId = System.UInt32;

using JamUnity.World.Runtime;

namespace JamUnity.Actor.Runtime
{
    [DisallowMultipleComponent]
    public class ActorManager : MonoBehaviour
    {
        [SerializeField] private ActorPresentationCatalog actorPresentationCatalog;
        [SerializeField] private Transform defaultParent;

        public ActorPresentationCatalog ActorPresentationCatalog => actorPresentationCatalog;
        public Transform DefaultParent => defaultParent;

        public void Configure(ActorPresentationCatalog catalog)
        {
            actorPresentationCatalog = catalog;
        }

		public bool TryInstantiate(ulong actorArchetypeKey, ActorId actorId, out GameObject instance)
		{
			return TryInstantiate(actorArchetypeKey, actorId, defaultParent, out instance);
		}

		public bool TryInstantiate(ulong actorArchetypeKey, ActorId actorId, Transform parentOverride, out GameObject instance)
        {
            if (actorPresentationCatalog == null
                || !actorPresentationCatalog.TryGetActorPrefab(actorArchetypeKey, out GameObject prefab)
                || prefab == null)
            {
                instance = null;
                return false;
            }

            instance = Instantiate(prefab, parentOverride);
			instance.name = $"{prefab.name}_{actorId}";
            return true;
        }

        protected virtual void Awake()
        {
            ResolveDefaultParent();
        }

        protected virtual void OnValidate()
        {
            ResolveDefaultParent();
        }

        private void ResolveDefaultParent()
        {
            if (defaultParent != null)
                return;

            WorldRoot worldRoot = GetComponent<WorldRoot>();
            if (worldRoot != null)
                defaultParent = worldRoot.transform;
        }
    }

} // namespace JamUnity.Actor.Runtime
