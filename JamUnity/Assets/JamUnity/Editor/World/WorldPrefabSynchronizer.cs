using System;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;
using UnityEditor;
using UnityEngine;

using JamUnity.Actor.Runtime;
using JamUnity.Authoring.Actor;
using JamUnity.Authoring.World;
using JamUnity.Editor;
using JamUnity.SharedData.Generated;
using JamUnity.World.Runtime;

namespace JamUnity.Editor.World.Runtime
{
    public static class WorldPrefabSynchronizer
    {
        private const string PrefabRoot = Core.Util.Path.WorldContentAssetRoot;

        public static bool TrySync(
            WorldArchetypeDatabase worldDatabase,
            ActorArchetypeDatabase actorDatabase,
            ActorPresentationCatalog presentationCatalog,
            IReadOnlyDictionary<string, string> actorLevelPaths,
            out string message)
        {
            if (worldDatabase == null)
            {
                message = "WorldArchetypeDatabase is null.";
                return false;
            }

            foreach (WorldArchetypeData worldArchetype in worldDatabase.Entries)
            {
                if (worldArchetype == null || string.IsNullOrWhiteSpace(worldArchetype.AssetName)
                    || actorDatabase == null
                    || presentationCatalog == null)
                    continue;

                string prefabPath = $"{PrefabRoot}/{worldArchetype.AssetName}.prefab";
                EnsureWorldPrefab(prefabPath, worldArchetype);

                if (!actorLevelPaths.TryGetValue(worldArchetype.ActorLevelName, out string levelPath))
                    continue;

                if (!TryLoadActorLevel(levelPath, out ActorLevelsRootDto level, out message))
                    return false;

                if (!TrySyncLevelActors(prefabPath, worldArchetype, actorDatabase, presentationCatalog, level, out message))
                    return false;
            }

            message = "Synchronized world prefabs and level actor instances.";
            return true;
        }

        private static void EnsureWorldPrefab(string path, WorldArchetypeData archetype)
        {
            if (AssetDatabase.LoadAssetAtPath<GameObject>(path) == null)
            {
                AssetEditorUtil.EnsureFolderHierarchy(PrefabRoot);
                GameObject world = new(archetype.AssetName);
                ConfigureWorldRoot(world, archetype);
                PrefabUtility.SaveAsPrefabAsset(world, path);
                UnityEngine.Object.DestroyImmediate(world);
                return;
            }

            GameObject root = PrefabUtility.LoadPrefabContents(path);
            try
            {
                ConfigureWorldRoot(root, archetype);
                PrefabUtility.SaveAsPrefabAsset(root, path);
            }
            finally
            {
                PrefabUtility.UnloadPrefabContents(root);
            }
        }

        private static void ConfigureWorldRoot(GameObject root, WorldArchetypeData archetype)
        {
            WorldPresenter  presenter = root.GetComponent<WorldPresenter>() ?? root.AddComponent<WorldPresenter>();
            ActorManager    manager   = root.GetComponent<ActorManager>() ?? root.AddComponent<ActorManager>();
            WorldRoot       worldRoot = root.GetComponent<WorldRoot>() ?? root.AddComponent<WorldRoot>();
            WorldAuthoring  authoring = root.GetComponent<WorldAuthoring>() ?? root.AddComponent<WorldAuthoring>();

            worldRoot.Configure();
            authoring.Configure(archetype, worldRoot);
        }

        private static bool TryLoadActorLevel(string manifestRelativePath, out ActorLevelsRootDto level, out string message)
        {
            level = null;
            string path = JamUnity.Core.Util.Path.ResolveSharedDataPath(manifestRelativePath);
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            {
                message = $"Actor level file '{manifestRelativePath}' was not found.";
                return false;
            }

            try
            {
                level = JsonConvert.DeserializeObject<ActorLevelsRootDto>(File.ReadAllText(path));
            }
            catch (Exception exception)
            {
                message = $"Could not read actor level '{manifestRelativePath}': {exception.Message}";
                return false;
            }

            if (level == null || level.version != 1 || level.instances == null)
            {
                message = $"Actor level '{manifestRelativePath}' has an unsupported format.";
                return false;
            }

            message = string.Empty;
            return true;
        }

        private static bool TrySyncLevelActors(string prefabPath, WorldArchetypeData worldArchetype, ActorArchetypeDatabase actorDatabase, ActorPresentationCatalog presentationCatalog, ActorLevelsRootDto level, out string message)
        {
            var validatedIds = new HashSet<uint>();
            foreach (ActorLevelInstanceDto instance in level.instances)
            {
				if (instance == null || !ActorLevelAuthoring.IsCanonicalActorId(instance.actorId) || !validatedIds.Add(instance.actorId)
                    || !actorDatabase.TryGetArchetype(instance.actorArchetype, out _)
                    || !presentationCatalog.TryGetActorPrefab(instance.actorArchetype, out _)
                    || !TryGetPose(instance, out _, out _))
                {
                    message = $"World '{worldArchetype.AssetName}' has an invalid actor-level instance.";
                    return false;
                }
            }

            GameObject root = PrefabUtility.LoadPrefabContents(prefabPath);
            try
            {
                WorldRoot worldRoot = root.GetComponent<WorldRoot>();
                Dictionary<uint, ActorLevelAuthoring> existing = CollectLevelActors(worldRoot.transform);
                var expected = new HashSet<uint>();
                ActorLevelAuthoring[] existingAll = worldRoot.GetComponentsInChildren<ActorLevelAuthoring>(true);
                var retained = new HashSet<ActorLevelAuthoring>();

                foreach (ActorLevelInstanceDto instance in level.instances)
                {
					if (instance == null || !ActorLevelAuthoring.IsCanonicalActorId(instance.actorId) || !expected.Add(instance.actorId)
                        || !actorDatabase.TryGetArchetype(instance.actorArchetype, out ActorArchetypeData archetype)
                        || !presentationCatalog.TryGetActorPrefab(instance.actorArchetype, out GameObject actorPrefab)
                        || !TryGetPose(instance, out Vector3 position, out Quaternion rotation))
                        continue;

                    if (!existing.TryGetValue(instance.actorId, out ActorLevelAuthoring authoring)
                        || authoring.ActorArchetype != archetype)
                    {
                        if (authoring != null)
                            UnityEngine.Object.DestroyImmediate(authoring.gameObject);

                        GameObject actor = (GameObject)PrefabUtility.InstantiatePrefab(actorPrefab, worldRoot.transform);
                        authoring = actor.GetComponent<ActorLevelAuthoring>() ?? actor.AddComponent<ActorLevelAuthoring>();
                    }

                    authoring.Configure(instance.actorId, archetype);
                    authoring.transform.SetLocalPositionAndRotation(position, rotation);
                    existing.Remove(instance.actorId);
                    retained.Add(authoring);
                }

                foreach (ActorLevelAuthoring stale in existingAll)
                {
                    if (stale != null && !retained.Contains(stale))
                        UnityEngine.Object.DestroyImmediate(stale.gameObject);
                }

                PrefabUtility.SaveAsPrefabAsset(root, prefabPath);
            }
            finally
            {
                PrefabUtility.UnloadPrefabContents(root);
            }

            message = string.Empty;
            return true;
        }

        private static Dictionary<uint, ActorLevelAuthoring> CollectLevelActors(Transform root)
        {
            var result = new Dictionary<uint, ActorLevelAuthoring>();
            foreach (ActorLevelAuthoring authoring in root.GetComponentsInChildren<ActorLevelAuthoring>(true))
            {
                if (authoring.ActorId != 0 && !result.ContainsKey(authoring.ActorId))
                    result.Add(authoring.ActorId, authoring);
            }
            return result;
        }

        private static bool TryGetPose(ActorLevelInstanceDto instance, out Vector3 position, out Quaternion rotation)
        {
            position = default;
            rotation = default;
            if (instance.spawnPose?.p == null || instance.spawnPose.q == null
                || instance.spawnPose.p.Count != 3 || instance.spawnPose.q.Count != 4)
                return false;

            position = new Vector3(instance.spawnPose.p[0], instance.spawnPose.p[1], instance.spawnPose.p[2]);
            rotation = new Quaternion(instance.spawnPose.q[0], instance.spawnPose.q[1], instance.spawnPose.q[2], instance.spawnPose.q[3]);
            return true;
        }
    }
}
