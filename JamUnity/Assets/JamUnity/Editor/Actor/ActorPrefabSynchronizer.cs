using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;

using JamUnity.Actor.Runtime;
using JamUnity.Authoring.Actor;
using JamUnity.Authoring.Physics;
using JamUnity.Editor;

namespace JamUnity.Editor.Actor
{
    public static class ActorPrefabSynchronizer
    {
        private const string PrefabRoot = Core.Util.Path.ActorContentAssetRoot;

        public static bool TrySync(ActorPresentationCatalog presentationCatalog, IEnumerable<ActorArchetypeDatabase> archetypeDatabases, out string message)
        {
            if (presentationCatalog == null || archetypeDatabases == null)
            {
                message = "ActorPresentationCatalog and actor archetype databases are required.";
                return false;
            }

            HashSet<string> synchronizedNames = new(System.StringComparer.OrdinalIgnoreCase);
            foreach (ActorArchetypeDatabase archetypeDatabase in archetypeDatabases)
            {
                if (archetypeDatabase == null)
                    continue;
                foreach (ActorArchetypeData archetype in archetypeDatabase.Archetypes)
                {
                    if (archetype == null || string.IsNullOrWhiteSpace(archetype.ArchetypeName)
                        || !synchronizedNames.Add(archetype.ArchetypeName)
                        || !presentationCatalog.TryGetEntry(archetype.ArchetypeName, out ActorPresentationCatalog.Entry entry))
                        continue;

                    if (archetype.PhysicsArchetype == null)
                    {
                        message = $"Actor archetype '{archetype.ArchetypeName}' has no PhysicsArchetypeData reference.";
                        return false;
                    }

                    string path = entry.ActorPrefab != null
                        ? AssetDatabase.GetAssetPath(entry.ActorPrefab)
                        : GetPrefabPath(archetype.ArchetypeName);
                    GameObject prefab = entry.ActorPrefab != null ? entry.ActorPrefab : AssetDatabase.LoadAssetAtPath<GameObject>(path);
                    if (prefab == null)
                        prefab = CreatePrefab(path, archetype);
                    else
                        EnsurePrefabSkeleton(path, archetype);

                    if (!HasExpectedPhysicsArchetype(path, archetype.PhysicsArchetype))
                    {
                        message = $"Actor prefab '{path}' did not retain the expected PhysicsArchetypeData reference for '{archetype.ArchetypeName}'.";
                        return false;
                    }

                    entry.ActorPrefab = prefab;
                }
            }

            EditorUtility.SetDirty(presentationCatalog);
            message = $"Synchronized {synchronizedNames.Count} actor prefabs.";
            return true;
        }

        private static string GetPrefabPath(string archetypeName)
        {
            return $"{PrefabRoot}/{archetypeName}.prefab";
        }

        private static GameObject CreatePrefab(string path, ActorArchetypeData archetype)
        {
            string directory = Path.GetDirectoryName(path)?.Replace('\\', '/');
            AssetEditorUtil.EnsureFolderHierarchy(directory);

            GameObject root = new(archetype.ArchetypeName);
            EnsureSkeleton(root, archetype);
            GameObject prefab = PrefabUtility.SaveAsPrefabAsset(root, path);
            Object.DestroyImmediate(root);
            return prefab;
        }

        private static void EnsurePrefabSkeleton(string path, ActorArchetypeData archetype)
        {
            GameObject root = PrefabUtility.LoadPrefabContents(path);
            try
            {
                EnsureSkeleton(root, archetype);
                PrefabUtility.SaveAsPrefabAsset(root, path);
            }
            finally
            {
                PrefabUtility.UnloadPrefabContents(root);
            }
        }

        private static void EnsureSkeleton(GameObject root, ActorArchetypeData archetype)
        {
            if (root.GetComponent<ActorRootMarker>() == null)
                root.AddComponent<ActorRootMarker>();

            ActorVisualMarker visual = root.GetComponentInChildren<ActorVisualMarker>(true);
            if (visual == null)
            {
                GameObject visualRoot = new("Visual");
                visualRoot.transform.SetParent(root.transform, false);
                visualRoot.AddComponent<ActorVisualMarker>();
            }

            ActorPhysicalMarker physical = root.GetComponentInChildren<ActorPhysicalMarker>(true);
            if (physical == null)
            {
                GameObject physicalRoot = new("Physical");
                physicalRoot.transform.SetParent(root.transform, false);
                physical = physicalRoot.AddComponent<ActorPhysicalMarker>();
            }

            PhysicsArchetypeAuthoring physics = physical.GetComponent<PhysicsArchetypeAuthoring>();
            if (physics == null)
                physics = physical.gameObject.AddComponent<PhysicsArchetypeAuthoring>();
            physics.SetPhysicsArchetype(archetype.PhysicsArchetype);
        }

        private static bool HasExpectedPhysicsArchetype(string path, PhysicsArchetypeData expected)
        {
            GameObject root = PrefabUtility.LoadPrefabContents(path);
            try
            {
                ActorPhysicalMarker physical = root.GetComponentInChildren<ActorPhysicalMarker>(true);
                PhysicsArchetypeAuthoring physics = physical != null
                    ? physical.GetComponent<PhysicsArchetypeAuthoring>()
                    : null;
                return physics != null && physics.PhysicsArchetype == expected;
            }
            finally
            {
                PrefabUtility.UnloadPrefabContents(root);
            }
        }
    }
}
