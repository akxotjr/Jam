using UnityEngine;
using JamUnity.Actor.Runtime;

namespace JamUnity.World.Runtime
{
    [DisallowMultipleComponent]
    [RequireComponent(typeof(WorldPresenter), typeof(ActorManager))]
    [AddComponentMenu("JamUnity/World/World Root")]
    public sealed class WorldRoot : MonoBehaviour
    {
        [SerializeField] private WorldPresenter worldPresenter;
        [SerializeField] private ActorManager actorManager;

        public WorldPresenter WorldPresenter => worldPresenter;
        public ActorManager ActorManager => actorManager;
        public bool IsRuntimeReady => worldPresenter != null && actorManager != null;

        public void Configure()
        {
            if (worldPresenter == null)
                worldPresenter = GetComponent<WorldPresenter>();
            if (actorManager == null)
                actorManager = GetComponent<ActorManager>();
        }

        private void OnValidate()
        {
            if (worldPresenter == null)
                worldPresenter = GetComponent<WorldPresenter>();

            if (actorManager == null)
                actorManager = GetComponent<ActorManager>();

            if (actorManager == null)
                actorManager = gameObject.AddComponent<ActorManager>();
        }
    }

} // namespace JamUnity.World.Runtime
