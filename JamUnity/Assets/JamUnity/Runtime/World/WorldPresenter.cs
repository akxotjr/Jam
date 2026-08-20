using System.Collections.Generic;
using UnityEngine;

using JamUnity.Core.Native;
using JamUnity.Core.Util;
using JamUnity.Actor.Runtime;
using JamUnity.Authoring.Actor;
using ActorId = System.UInt32;

namespace JamUnity.World.Runtime
{
    public class WorldPresenter : MonoBehaviour
    {
        public readonly struct ActorRenderSample
        {
			public readonly ActorId     ActorId;
            public readonly ulong       ActorArchetypeKey;
            public readonly bool        IsLocal;
            public readonly bool        HasTransform;
            public readonly Vector3     Position;
            public readonly Quaternion  Rotation;
            

            public ActorRenderSample(
				ActorId actorId,
                ulong actorArchetypeKey,
                bool isLocal,
                bool hasTransform,
                Vector3 position,
                Quaternion rotation)
            {
				ActorId           = actorId;
                ActorArchetypeKey = actorArchetypeKey;
                IsLocal = isLocal;
                HasTransform = hasTransform;
                Position = position;
                Rotation = rotation;
            }
        }

        private class ActorView
        {
            public GameObject go;
            public ulong actorArchetypeKey;
            public bool isLevelActor;
            public bool isLocal;
            public bool hasPresentedTransform;
        }
        
        [SerializeField] private ActorManager   actorManager;
        [SerializeField] private WorldRoot      worldRoot;
    
        private readonly Dictionary<ActorId, ActorView> actors = new();
        private readonly Dictionary<ulong, Stack<ActorView>> pooledActors = new();
        private float localActorSmoothSpeed;

        public void ConfigureLocalActorSmoothing(float smoothSpeed)
        {
            localActorSmoothSpeed = Mathf.Max(0.0f, smoothSpeed);
        }
    
        private void Awake()
        {
            if (worldRoot == null)
                worldRoot = GetComponent<WorldRoot>();
    
            if (actorManager == null)
                actorManager = GetComponent<ActorManager>();

            RegisterAuthoredActors();
        }
    
        private void OnValidate()
        {
            if (worldRoot == null)
                worldRoot = GetComponent<WorldRoot>();
    
            if (actorManager == null)
                actorManager = GetComponent<ActorManager>();
        }
    
        public Transform GetLocalActorTransform()
        {
            foreach (var actor in actors.Values)
            {
                if (actor.isLocal)
                    return actor.go.transform;
            }
    
            return null;
        }
    
        public void OnActorSpawned(ActorId actorId, ulong actorArchetypeKey, bool isLocal)
        {
			if (actorId == 0)
				return;

			if (actors.TryGetValue(actorId, out var existing))
            {
                if (!existing.isLevelActor
                    && existing.actorArchetypeKey == 0
                    && actorArchetypeKey != 0)
                    TryReplaceActorObject(existing, actorId, actorArchetypeKey);
    
                existing.actorArchetypeKey = actorArchetypeKey;
                existing.isLocal = isLocal;
                return;
            }

            if (TryRentActor(actorArchetypeKey, out ActorView pooled))
            {
                pooled.actorArchetypeKey = actorArchetypeKey;
                pooled.isLocal = isLocal;
                pooled.hasPresentedTransform = false;
                pooled.go.name = $"Actor_{actorId}";
                pooled.go.SetActive(true);
                actors[actorId] = pooled;
                return;
            }
    
            GameObject go = CreateActorObject(actorId, actorArchetypeKey);
            if (go == null)
                return;
    
			actors[actorId] = new ActorView
            {
                go = go,
                actorArchetypeKey = actorArchetypeKey,
                isLocal = isLocal
            };
        }
    
        public void OnActorDespawned(ActorId actorId)
        {
			if (!actors.TryGetValue(actorId, out var actor))
                return;

            if (actor.isLevelActor)
                return;

            Destroy(actor.go);
			actors.Remove(actorId);
        }

        public void PoolActorPresentation(ActorId actorId)
        {
            if (!actors.TryGetValue(actorId, out ActorView actor)
                || actor == null
                || actor.isLevelActor)
            {
                return;
            }

            actors.Remove(actorId);
            if (actor.go == null)
                return;

            actor.isLocal = false;
            actor.hasPresentedTransform = false;
            actor.go.SetActive(false);

            if (!pooledActors.TryGetValue(actor.actorArchetypeKey, out Stack<ActorView> pool))
            {
                pool = new Stack<ActorView>();
                pooledActors.Add(actor.actorArchetypeKey, pool);
            }
            pool.Push(actor);
        }
    
        public void ApplyActorRenderSample(in ActorRenderSample sample, float deltaTime)
        {
            if (sample.ActorId == 0 || !sample.HasTransform)
                return;

			if (!actors.TryGetValue(sample.ActorId, out var actor))
            {
                OnActorSpawned(sample.ActorId, sample.ActorArchetypeKey, sample.IsLocal);

				if (!actors.TryGetValue(sample.ActorId, out actor))
                    return;
            }

            if (!actor.isLevelActor
                && actor.actorArchetypeKey == 0
                && sample.ActorArchetypeKey != 0)
                TryReplaceActorObject(actor, sample.ActorId, sample.ActorArchetypeKey);

            actor.actorArchetypeKey     = sample.ActorArchetypeKey != 0 ? sample.ActorArchetypeKey : actor.actorArchetypeKey;
            actor.isLocal               = sample.IsLocal;

            Vector3 presentedPosition = sample.Position;
            if (actor.isLocal && actor.hasPresentedTransform)
            {
                float blend = 1.0f - Mathf.Exp(-localActorSmoothSpeed * deltaTime);
                presentedPosition = Vector3.Lerp(actor.go.transform.position, sample.Position, blend);
            }

            actor.go.transform.position = presentedPosition;
            actor.go.transform.rotation = sample.Rotation;
            actor.hasPresentedTransform = true;
        }

        public void ResetRuntimePopulation()
        {
            List<ActorId> runtimeActorIds = new();
            foreach (KeyValuePair<ActorId, ActorView> entry in actors)
            {
                ActorId actorId = entry.Key;
                ActorView actor = entry.Value;
                if (actor == null || actor.isLevelActor)
                    continue;

                if (actor.go != null)
                    Destroy(actor.go);
                runtimeActorIds.Add(actorId);
            }

            foreach (ActorId actorId in runtimeActorIds)
                actors.Remove(actorId);

            foreach (Stack<ActorView> pool in pooledActors.Values)
            {
                while (pool.Count > 0)
                {
                    ActorView actor = pool.Pop();
                    if (actor?.go != null)
                        Destroy(actor.go);
                }
            }
            pooledActors.Clear();
        }

        private bool TryRentActor(ulong actorArchetypeKey, out ActorView actor)
        {
            if (pooledActors.TryGetValue(actorArchetypeKey, out Stack<ActorView> pool))
            {
                while (pool.Count > 0)
                {
                    actor = pool.Pop();
                    if (actor?.go != null)
                        return true;
                }
            }

            actor = null;
            return false;
        }

        private void RegisterAuthoredActors()
        {
            actors.Clear();

            foreach (ActorLevelAuthoring authored in GetComponentsInChildren<ActorLevelAuthoring>(true))
            {
                if (!authored.ExportEnabled)
                    continue;

                ActorId actorId = ActorLevelAuthoring.NormalizeLegacyActorId(authored.ActorId);
                if (!ActorLevelAuthoring.IsCanonicalActorId(actorId))
                {
                    Debug.LogWarning($"[WorldPresentation] authored actor '{authored.name}' has invalid actorId={authored.ActorId}.", authored);
                    continue;
                }

                if (actors.ContainsKey(actorId))
                {
                    Debug.LogWarning($"[WorldPresentation] duplicate authored actorId={actorId} on '{authored.name}'.", authored);
                    continue;
                }

                string archetypeName = authored.ActorArchetype != null
                    ? authored.ActorArchetype.ArchetypeName
                    : string.Empty;
                ulong actorArchetypeKey = string.IsNullOrWhiteSpace(archetypeName)
                    ? 0
                    : StableKey.MakeStableKey(archetypeName);

                actors.Add(actorId, new ActorView
                {
                    go = authored.gameObject,
                    actorArchetypeKey = actorArchetypeKey,
                    isLevelActor = true,
                    isLocal = false,
                });
            }
        }
    
        private GameObject CreateActorObject(ActorId actorId, ulong actorArchetypeKey)
        {
            Transform parent = ResolveParent();
            if (actorManager != null
                && actorArchetypeKey != 0
				&& actorManager.TryInstantiate(actorArchetypeKey, actorId, parent, out var go))
            {
                return go;
            }

            return null;
        }
    
        private bool TryReplaceActorObject(ActorView actor, ActorId actorId, ulong actorArchetypeKey)
        {
            Transform parent = ResolveParent();
            if (actorManager == null
				|| !actorManager.TryInstantiate(actorArchetypeKey, actorId, parent, out var replacement))
            {
                return false;
            }
    
            Transform oldTransform = actor.go.transform;
            replacement.transform.SetPositionAndRotation(oldTransform.position, oldTransform.rotation);
            replacement.transform.localScale = oldTransform.localScale;
    
            Destroy(actor.go);
            actor.go = replacement;
            return true;
        }
    
        private Transform ResolveParent()
        {
            if (worldRoot == null)
                worldRoot = GetComponent<WorldRoot>();
    
            if (worldRoot == null)
                return null;
    
            return worldRoot.transform;
        }
    }

} // namespace JamUnity.World.Runtime
