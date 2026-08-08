using JamUnity.Core.Native;
using JamUnity.Actor.Runtime;
using JamUnity.World.Presentation;
using JamUnity.World.Runtime;
using System;
using System.Collections.Generic;
using UnityEngine;
using Object = UnityEngine.Object;
using ActorId = System.UInt32;

namespace JamUnity.Runtime.Client
{
    public sealed class WorldManager : INativeWorldEventSink, INativeActorEventSink
    {
        public struct Config
        {
            public Transform PresentationRoot;
            public WorldPresentationDatabase WorldPresentationDatabase;
            public ActorPresentationCatalog ActorPresentationCatalog;
            public float LocalActorSmoothSpeed;
        }

        private CoreNative.ActorState[] previousActorBuffer = new CoreNative.ActorState[512];
        private CoreNative.ActorState[] currentActorBuffer = new CoreNative.ActorState[512];
        private readonly Dictionary<ActorId, int> previousActorIndices = new(512);
        private readonly Dictionary<(ulong WorldId, ActorId ActorId), ActorPresentationIdentity> actorIdentities = new(512);

        private Config          config;
        private WorldRoot       activeWorldRoot;
        private WorldPresenter  activeWorldPresenter;
        private GroundClickMarker groundClickMarker;
        private ulong           activeMainWorldId;
        private ulong           activeMainWorldInstanceId;
        private ulong           activeMainWorldArchetypeKey;
        private ulong           lastMainWorldId             = ulong.MaxValue;
        private ulong           lastMainWorldInstanceId     = ulong.MaxValue;
        private ulong           lastMainWorldArchetypeKey   = ulong.MaxValue;

        public event Action<CoreNative.WorldRuntimeRef> MainWorldActivated;
        public event Action<ulong, bool> LocalPlayerAvailabilityChanged;
        private readonly HashSet<ulong> localPlayerWorlds = new();

        public bool HasLocalPlayer(ulong worldId) => localPlayerWorlds.Contains(worldId);

        private struct ActorPresentationIdentity
        {
            public ulong ActorArchetypeKey;
            public bool  IsLevelActor;
            public bool  IsLocal;
        }

        public void Init(Config config)
        {
            this.config = config;
            Debug.Log($"[WorldPresentation] initialized. worldDatabase={(config.WorldPresentationDatabase != null ? config.WorldPresentationDatabase.name : "null")}, actorCatalog={(config.ActorPresentationCatalog != null ? config.ActorPresentationCatalog.name : "null")}, root={(config.PresentationRoot != null ? config.PresentationRoot.name : "null")}");
        }

        public void ConfigureLocalActorSmoothing(float smoothSpeed)
        {
            config.LocalActorSmoothSpeed = smoothSpeed;
            activeWorldPresenter?.ConfigureLocalActorSmoothing(smoothSpeed);
        }

        public void Tick(NativeManager nativeManager, float deltaTime)
        {
            if (nativeManager == null || !nativeManager.IsInitialized)
                return;

            SyncMainWorld(nativeManager);
            PullPresentationFrames(nativeManager, deltaTime);
        }

        public void OnMainWorldChanged(in CoreNative.WorldRuntimeRef world)
        {
            Debug.Log($"[WorldPresentation] main world changed. worldId={world.worldId}, instance={world.worldInstanceId}, archetype={world.worldArchetypeKey}");

            if (world.worldArchetypeKey == 0)
            {
                Debug.Log("[WorldPresentation] main world is empty; unloading active presentation.");
                UnloadActiveWorld();
                activeMainWorldId = world.worldId;
                activeMainWorldInstanceId = world.worldInstanceId;
                activeMainWorldArchetypeKey = 0;
                return;
            }

            if (activeWorldRoot != null
                && activeMainWorldId == world.worldId
                && activeMainWorldInstanceId == world.worldInstanceId
                && activeMainWorldArchetypeKey == world.worldArchetypeKey)
            {
                return;
            }

            if (!TrySwitchWorld(world.worldArchetypeKey))
                return;

            activeMainWorldId = world.worldId;
            activeMainWorldInstanceId = world.worldInstanceId;
            activeMainWorldArchetypeKey = world.worldArchetypeKey;
            MainWorldActivated?.Invoke(world);
        }

        public void OnWorldParticipantChanged(CoreNative.WorldParticipantChangedEvent ev)
        {
            Debug.Log($"[WorldPresentation] participant changed. change={ev.change}, worldId={ev.world.worldId}, instance={ev.world.worldInstanceId}, archetype={ev.world.worldArchetypeKey}, participant={ev.participantUserId}");
        }

        public void OnWorldRayResolved(CoreNative.WorldRayResolvedEvent ev)
        {
            if (ev.hit == 0 || ev.worldId != activeMainWorldId)
                return;

            groundClickMarker?.Show(ev.position.ToUnity(), ev.normal.ToUnity());
        }

        public void OnActorLifecycleChanged(CoreNative.ActorLifecycleChangedEvent ev)
        {
            if (ev.reason is CoreNative.eActorLifecycleReason.Spawned or CoreNative.eActorLifecycleReason.AoiEntered)
            {
                if (ev.isLocal != 0 && localPlayerWorlds.Add(ev.worldId))
                    LocalPlayerAvailabilityChanged?.Invoke(ev.worldId, true);
                actorIdentities[(ev.worldId, ev.actorId)] = new ActorPresentationIdentity
                {
                    ActorArchetypeKey = ev.actorArchetypeKey,
                    IsLocal = ev.isLocal != 0,
                };
                if (ev.worldId == activeMainWorldId)
                    activeWorldPresenter?.OnActorSpawned(ev.actorId, ev.actorArchetypeKey, ev.isLocal != 0);
                return;
            }

            if (ev.reason is CoreNative.eActorLifecycleReason.Despawned or CoreNative.eActorLifecycleReason.AoiLeft or CoreNative.eActorLifecycleReason.LocallyHidden)
            {
                bool wasLocal = ev.isLocal != 0
                    || (actorIdentities.TryGetValue((ev.worldId, ev.actorId), out ActorPresentationIdentity identity)
                        && identity.IsLocal);
                if (wasLocal && localPlayerWorlds.Remove(ev.worldId))
                    LocalPlayerAvailabilityChanged?.Invoke(ev.worldId, false);
                actorIdentities.Remove((ev.worldId, ev.actorId));
                if (ev.worldId == activeMainWorldId)
                    activeWorldPresenter?.OnActorDespawned(ev.actorId);
            }
        }

        private void SyncMainWorld(NativeManager nativeManager)
        {
            CoreNative.WorldRuntimeRef world = nativeManager.TryGetMainWorldRef(out CoreNative.WorldRuntimeRef value) ? value : default;
            if (lastMainWorldId == world.worldId
                && lastMainWorldInstanceId == world.worldInstanceId
                && lastMainWorldArchetypeKey == world.worldArchetypeKey)
                return;

            lastMainWorldId = world.worldId;
            lastMainWorldInstanceId = world.worldInstanceId;
            lastMainWorldArchetypeKey = world.worldArchetypeKey;
            Debug.Log($"[WorldPresentation] native main world observed. worldId={world.worldId}, instance={world.worldInstanceId}, archetype={world.worldArchetypeKey}");
            OnMainWorldChanged(world);
        }

        private void PullPresentationFrames(NativeManager nativeManager, float deltaTime)
        {
            CoreNative.eResult result = nativeManager.CopyActorFramePair(
                activeMainWorldId,
                previousActorBuffer,
                previousActorBuffer.Length,
                out CoreNative.ActorFrame previousFrame,
                currentActorBuffer,
                currentActorBuffer.Length,
                out CoreNative.ActorFrame currentFrame,
                out CoreNative.FrameCopyInfo copyInfo);
            if (result == CoreNative.eResult.BufferTooSmall)
            {
                ResizeActorBuffers(copyInfo);
                result = nativeManager.CopyActorFramePair(
                    activeMainWorldId,
                    previousActorBuffer,
                    previousActorBuffer.Length,
                    out previousFrame,
                    currentActorBuffer,
                    currentActorBuffer.Length,
                    out currentFrame,
                    out _);
            }

            if (result != CoreNative.eResult.Ok || currentFrame.sequence == 0)
                return;

            RecordPresentationFrame(currentFrame.sequence);
            float interpolationAlpha = ResolvePresentationInterpolationAlpha(previousFrame, currentFrame);

            int actorCount = currentFrame.actorCount;

            previousActorIndices.Clear();
            int previousActorCount = previousFrame.actorCount;

            for (int i = 0; i < previousActorCount; ++i)
				previousActorIndices[previousActorBuffer[i].actorId] = i;

            for (int i = 0; i < actorCount; ++i)
            {
                CoreNative.ActorState current = currentActorBuffer[i];
				bool hasPrevious = previousActorIndices.TryGetValue(current.actorId, out int previousIndex);
                CoreNative.ActorState previous = hasPrevious && previousIndex < previousActorCount
                    ? previousActorBuffer[previousIndex]
                    : default;

                WorldPresenter.ActorRenderSample sample = BuildRenderSample(previous, current, interpolationAlpha);
                activeWorldPresenter?.ApplyActorRenderSample(sample, deltaTime);
            }
        }

        private ulong lastPresentationSequence;
        private int presentationFrames;
        private int presentationRepeatedFrames;
        private int presentationSequenceChanges;
        private float presentationLogElapsed;
        private ulong presentationInterpolationSequence;
        private float presentationInterpolationElapsed;

        private float ResolvePresentationInterpolationAlpha(
            CoreNative.ActorFrame previousFrame,
            CoreNative.ActorFrame currentFrame)
        {
            if (previousFrame.sequence == 0
                || currentFrame.sequence == 0
                || currentFrame.sequence <= previousFrame.sequence)
                return 1.0f;

            if (currentFrame.sequence != presentationInterpolationSequence)
            {
                presentationInterpolationSequence = currentFrame.sequence;
                presentationInterpolationElapsed = 0.0f;
            }
            else
            {
                presentationInterpolationElapsed += Time.unscaledDeltaTime;
            }

            const float simulationTickInterval = 1.0f / 30.0f;
            return Mathf.Clamp01(presentationInterpolationElapsed / simulationTickInterval);
        }

        private void RecordPresentationFrame(ulong sequence)
        {
            ++presentationFrames;
            if (sequence == lastPresentationSequence)
                ++presentationRepeatedFrames;
            else
            {
                lastPresentationSequence = sequence;
                ++presentationSequenceChanges;
            }

            presentationLogElapsed += Time.unscaledDeltaTime;
            if (presentationLogElapsed < 1.0f)
                return;

            Debug.Log($"[MovementDiag][Presentation] frames={presentationFrames}, repeated={presentationRepeatedFrames}, sequenceChanges={presentationSequenceChanges}, lastSequence={lastPresentationSequence}");
            presentationFrames = 0;
            presentationRepeatedFrames = 0;
            presentationSequenceChanges = 0;
            presentationLogElapsed = 0.0f;
        }

        private void ResizeActorBuffers(CoreNative.FrameCopyInfo copyInfo)
        {
            int previousCapacity = ResolveBufferCapacity(previousActorBuffer.Length, copyInfo.previousRequiredCount);
            int currentCapacity = ResolveBufferCapacity(currentActorBuffer.Length, copyInfo.currentRequiredCount);
            if (previousCapacity != previousActorBuffer.Length)
                Array.Resize(ref previousActorBuffer, previousCapacity);
            if (currentCapacity != currentActorBuffer.Length)
                Array.Resize(ref currentActorBuffer, currentCapacity);
        }

        private static int ResolveBufferCapacity(int currentCapacity, int requiredCapacity)
        {
            if (requiredCapacity <= currentCapacity)
                return currentCapacity;

            int capacity = Math.Max(currentCapacity, 1);
            while (capacity < requiredCapacity && capacity <= int.MaxValue / 2)
                capacity <<= 1;

            return capacity < requiredCapacity ? requiredCapacity : capacity;
        }

        private bool TrySwitchWorld(ulong archetypeKey)
        {
            Debug.Log($"[WorldPresentation] switching presentation. archetype={archetypeKey}");
            UnloadActiveWorld();

            if (config.WorldPresentationDatabase == null)
            {
                Debug.LogError("[WorldPresentation] switch failed; WorldPresentationDatabase is null.");
                return false;
            }

            if (!config.WorldPresentationDatabase.TryGetEntry(archetypeKey, out WorldPresentationDatabase.Entry entry))
            {
                Debug.LogWarning($"[WorldPresentation] switch failed; no database entry for archetype={archetypeKey}, entries={config.WorldPresentationDatabase.Entries.Count}.");
                return false;
            }

            GameObject worldPrefab = entry.WorldPrefab;
            if (worldPrefab == null)
            {
                Debug.LogWarning($"[WorldPresentation] switch failed; prefab is null. archetype={archetypeKey}, name={entry.WorldArchetypeName}.");
                return false;
            }

            Transform presentationRoot = config.PresentationRoot != null ? config.PresentationRoot : null;
            GameObject instanceObject = Object.Instantiate(worldPrefab, presentationRoot, false);
            instanceObject.name = worldPrefab.name;

            WorldRoot instance = instanceObject.GetComponent<WorldRoot>();
            if (instance == null)
            {
                Object.Destroy(instanceObject);
                Debug.LogWarning($"[WorldPresentation] switch failed; prefab '{worldPrefab.name}' is missing WorldRoot.");
                return false;
            }

            ActorManager actorManager = instance.ActorManager;
            if (actorManager == null)
            {
                Object.Destroy(instance.gameObject);
                Debug.LogWarning($"[WorldPresentation] switch failed; world root '{instance.name}' is missing ActorManager.");
                return false;
            }

            actorManager.Configure(config.ActorPresentationCatalog);

            activeWorldRoot = instance;
            activeWorldPresenter = instance.WorldPresenter;
            groundClickMarker = instance.GetComponentInChildren<GroundClickMarker>(true);
            activeWorldPresenter?.ConfigureLocalActorSmoothing(config.LocalActorSmoothSpeed);
            activeWorldPresenter?.ResetRuntimePopulation();
            if (activeWorldPresenter == null)
            {
                Debug.LogWarning($"[WorldPresentation] switch failed; world root '{instance.name}' has no WorldPresenter.");
                return false;
            }

            Debug.Log($"[WorldPresentation] switch completed. archetype={archetypeKey}, prefab={worldPrefab.name}, instance={instance.name}");
            return true;
        }

        private void UnloadActiveWorld()
        {
            if (activeWorldRoot != null)
                Object.Destroy(activeWorldRoot.gameObject);

            activeWorldRoot = null;
            activeWorldPresenter = null;
            groundClickMarker = null;
            previousActorIndices.Clear();
            presentationInterpolationSequence = 0;
            presentationInterpolationElapsed = 0.0f;
        }

        private WorldPresenter.ActorRenderSample BuildRenderSample(
            CoreNative.ActorState previous,
            CoreNative.ActorState current,
            float interpolationAlpha)
        {
			actorIdentities.TryGetValue((activeMainWorldId, current.actorId), out ActorPresentationIdentity identity);

            Vector3 position = current.position.ToUnity();
            Quaternion rotation = current.rotation.ToUnity();
            if (previous.hasTransform != 0 && current.hasTransform != 0)
            {
                position = Vector3.Lerp(previous.position.ToUnity(), position, interpolationAlpha);
                rotation = Quaternion.Slerp(previous.rotation.ToUnity(), rotation, interpolationAlpha);
            }

			bool isLocal = current.isLocal != 0 || identity.IsLocal;
            ulong actorArchetypeKey = identity.ActorArchetypeKey;

            // if (isLocal)
            //     Debug.Log($"[MovementDiag][PlayerPosition] pos={position}");

            return new WorldPresenter.ActorRenderSample(
				current.actorId,
                actorArchetypeKey,
                isLocal,
                current.hasTransform != 0,
                position,
                rotation);
        }

    }
}
