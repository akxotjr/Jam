using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Newtonsoft.Json.Linq;
using UnityEditor;
using UnityEngine;

using JamUnity.Actor.Runtime;
using JamUnity.Authoring.Actor;
using JamUnity.Authoring.Physics;
using JamUnity.Authoring.World;
using JamUnity.Editor.Actor;
using JamUnity.Editor.Physics;
using JamUnity.Editor.World;
using JamUnity.Editor.World.Presentation;
using JamUnity.Editor.World.Runtime;
using JamUnity.World.Presentation;

namespace JamUnity.Editor.SharedData
{
    [InitializeOnLoad]
    public static class SharedDataStartupSync
    {
        private const string StartupSyncSessionKey  = "JamUnity.SharedDataStartupSync.Completed";
        private const string ActorCacheRoot         = Core.Util.Path.GeneratedAssetRoot + "/Actor";
        private const string PhysicsCacheRoot       = Core.Util.Path.GeneratedAssetRoot + "/Physics";
        private const string PhysicsSourceRoot      = "Physics/Source";

        private readonly struct DomainPaths
        {
            public readonly string PhysicsKey;
            public readonly string PhysicsAssets;

            public DomainPaths(string physicsKey, string physicsAssets)
            {
                PhysicsKey = physicsKey;
                PhysicsAssets = physicsAssets;
            }
        }

        private readonly struct ManifestPaths
        {
            public readonly string WorldTemplates;
            public readonly string WorldArchetypes;
            public readonly string ActorArchetypes;
            public readonly List<DomainPaths> Domains;
            public readonly Dictionary<string, string> ActorLevels;

            public ManifestPaths(string worldTemplates, string worldArchetypes, string actorArchetypes, List<DomainPaths> domains, Dictionary<string, string> actorLevels)
            {
                WorldTemplates = worldTemplates;
                WorldArchetypes = worldArchetypes;
                ActorArchetypes = actorArchetypes;
                Domains = domains;
                ActorLevels = actorLevels;
            }
        }

        static SharedDataStartupSync()
        {
            EditorApplication.delayCall += RunStartupSync;
        }

        [MenuItem("Tools/JamUnity/Sync/Refresh Shared Data Cache")]
        public static void RefreshSharedDataCacheMenu()
        {
            RunSync(logSuccess: true);
        }

        private static void RunStartupSync()
        {
            if (SessionState.GetBool(StartupSyncSessionKey, false))
                return;

            SessionState.SetBool(StartupSyncSessionKey, true);
            RunSync(logSuccess: false);
        }

        private static void RunSync(bool logSuccess)
        {
            if (!TryReadManifestPaths(out ManifestPaths manifestPaths, out string message))
            {
                Debug.LogError($"Shared data startup sync skipped: {message}");
                return;
            }

            if (!TryGetOrCreateAsset(WorldTemplateDatabase.DatabaseAssetPath, out WorldTemplateDatabase templateDatabase)
                || !TryGetOrCreateAsset(WorldArchetypeDatabase.DatabaseAssetPath, out WorldArchetypeDatabase worldArchetypeDatabase))
            {
                Debug.LogError("Shared data startup sync failed to create the world Cache databases.");
                return;
            }

            templateDatabase.SetWorldTemplateAssetPath(manifestPaths.WorldTemplates);
            worldArchetypeDatabase.SetWorldArchetypesAssetPath(manifestPaths.WorldArchetypes);
            EditorUtility.SetDirty(templateDatabase);
            EditorUtility.SetDirty(worldArchetypeDatabase);

            Dictionary<string, PhysicsAssetDatabase> physicsDatabases = new(StringComparer.Ordinal);
            foreach (DomainPaths domain in manifestPaths.Domains.OrderBy(static value => value.PhysicsKey == "Common" ? 0 : 1))
            {
                if (physicsDatabases.ContainsKey(domain.PhysicsKey))
                    continue;

                string physicsPath = $"{PhysicsCacheRoot}/{domain.PhysicsKey}/{domain.PhysicsKey}.asset";
                if (!TryGetOrCreateAsset(physicsPath, out PhysicsAssetDatabase physicsDatabase))
                {
                    Debug.LogError($"Shared data startup sync failed to create physics database '{domain.PhysicsKey}'.");
                    return;
                }

                physicsDatabase.SetSharedDataPath($"{PhysicsSourceRoot}/{domain.PhysicsKey}.physics_asset.json");
                PhysicsAssetDatabase includedDatabase = domain.PhysicsKey == "Common"
                    ? null
                    : physicsDatabases.GetValueOrDefault("Common");
                if (domain.PhysicsKey != "Common" && includedDatabase == null)
                {
                    Debug.LogError($"Physics source '{domain.PhysicsKey}' requires the Common physics database.");
                    return;
                }
                if (!PhysicsAssetImporter.TryImport(physicsDatabase, includedDatabase, out message))
                {
                    Debug.LogError($"Shared data startup physics sync failed for '{domain.PhysicsKey}': {message}");
                    return;
                }
                physicsDatabases.Add(domain.PhysicsKey, physicsDatabase);
            }

            string actorDatabasePath = $"{ActorCacheRoot}/ActorArchetypeDatabase.asset";
            if (!TryGetOrCreateAsset(actorDatabasePath, out ActorArchetypeDatabase actorDatabase))
            {
                Debug.LogError("Shared data startup sync failed to create the global actor database.");
                return;
            }
            actorDatabase.SetSharedDataPath(manifestPaths.ActorArchetypes);
            EditorUtility.SetDirty(actorDatabase);
            List<PhysicsAssetDatabase> actorPhysicsDatabases = physicsDatabases
                .OrderBy(static pair => pair.Key == "Common" ? 0 : 1)
                .Select(static pair => pair.Value)
                .ToList();
            if (!ActorArchetypeDatabaseImporter.TryImport(actorDatabase, actorPhysicsDatabases, out message))
            {
                Debug.LogError($"Shared data startup global actor sync failed: {message}");
                return;
            }

            if (!TryGetOrCreateAsset(Core.Util.Path.ActorPresentationCatalogAssetPath, out ActorPresentationCatalog presentationCatalog)
                || !ActorPresentationCatalogEditor.TrySyncFromActorArchetypes(presentationCatalog, out message))
            {
                Debug.LogError($"Shared data startup actor presentation sync failed: {message}");
                return;
            }

            if (!ActorPrefabSynchronizer.TrySync(presentationCatalog, new[] { actorDatabase }, out message))
            {
                Debug.LogError($"Shared data startup actor prefab sync failed: {message}");
                return;
            }

            AssetDatabase.SaveAssets();
            if (!WorldTemplateAssetImporter.TryImport(templateDatabase, out message)
                || !WorldArchetypeDatabaseImporter.TryImport(worldArchetypeDatabase, templateDatabase, physicsDatabases, out message))
            {
                Debug.LogError($"Shared data startup sync failed: {message}");
                return;
            }

            if (!WorldPrefabSynchronizer.TrySync(worldArchetypeDatabase, actorDatabase, presentationCatalog, manifestPaths.ActorLevels, out message))
            {
                Debug.LogError($"Shared data startup world prefab sync failed: {message}");
                return;
            }

            if (!TryGetOrCreateAsset(Core.Util.Path.WorldPresentationDatabaseAssetPath, out WorldPresentationDatabase worldPresentationDatabase)
                || !WorldPresentationDatabaseEditor.TrySyncFromCacheAndWorldAuthoring(worldPresentationDatabase, out message))
            {
                Debug.LogError($"Shared data startup presentation sync failed: {message}");
                return;
            }

            if (logSuccess)
                Debug.Log($"Synchronized {manifestPaths.Domains.Count} shared-data domains and world presentation tables.");
        }

        private static bool TryReadManifestPaths(out ManifestPaths paths, out string message)
        {
            // JamTools owns source JSON schema and semantic validation; Unity only consumes manifest routing.
            paths = default;
            string manifestPath = Core.Util.Path.SharedDataManifestPath;
            if (!File.Exists(manifestPath))
            {
                message = $"shared_data_manifest.json was not found at '{manifestPath}'.";
                return false;
            }

            JObject manifest;
            try
            {
                manifest = JObject.Parse(File.ReadAllText(manifestPath));
            }
            catch (Exception exception)
            {
                message = $"Could not read shared_data_manifest.json: {exception.Message}";
                return false;
            }

            string worldTemplates = ReadManifestPath(manifest, "bootstrap", "world_templates", "path");
            string worldArchetypes = ReadManifestPath(manifest, "bootstrap", "world_archetypes", "path");
            string actorArchetypes = ReadManifestPath(manifest, "bootstrap", "actor_archetypes", "path");
            if (!IsManifestRelativePath(worldTemplates) || !IsManifestRelativePath(worldArchetypes)
                || !IsManifestRelativePath(actorArchetypes))
            {
                message = "shared_data_manifest.json is missing a bootstrap path.";
                return false;
            }

            JObject physicsSets = manifest["content"]?["physics_asset_set"] as JObject;
            JObject actorLevels = manifest["content"]?["actor_level_set"] as JObject;
            if (physicsSets == null || !physicsSets.HasValues)
            {
                message = "shared_data_manifest.json must define physics_asset_set entries.";
                return false;
            }

            List<DomainPaths> domains = new();
            foreach (JProperty physicsSet in physicsSets.Properties())
            {
                string physicsKey = physicsSet.Name?.Trim() ?? string.Empty;
                string physicsPath = physicsSet.Value?["path"]?.Value<string>()?.Trim() ?? string.Empty;
                if (!IsDomainKey(physicsKey) || !IsManifestRelativePath(physicsPath))
                {
                    message = $"Physics domain '{physicsKey}' must have a safe name and manifest-relative path.";
                    return false;
                }

                domains.Add(new DomainPaths(physicsKey, physicsPath));
            }

            var actorLevelPaths = new Dictionary<string, string>(StringComparer.Ordinal);
            if (actorLevels != null)
            {
                foreach (JProperty actorLevel in actorLevels.Properties())
                {
                    string name = actorLevel.Name?.Trim() ?? string.Empty;
                    string path = actorLevel.Value?["path"]?.Value<string>()?.Trim() ?? string.Empty;
                    if (string.IsNullOrWhiteSpace(name) || !IsManifestRelativePath(path) || !actorLevelPaths.TryAdd(name, path))
                    {
                        message = "actor_level_set contains an invalid or duplicate entry.";
                        return false;
                    }
                }
            }

            paths = new ManifestPaths(worldTemplates, worldArchetypes, actorArchetypes, domains, actorLevelPaths);
            message = string.Empty;
            return true;
        }

        private static bool TryGetOrCreateAsset<T>(string assetPath, out T asset) where T : ScriptableObject
        {
            asset = AssetDatabase.LoadAssetAtPath<T>(assetPath);
            if (asset != null)
                return true;

            string directory = System.IO.Path.GetDirectoryName(assetPath)?.Replace('\\', '/');
            if (string.IsNullOrWhiteSpace(directory))
                return false;

            AssetEditorUtil.EnsureFolderHierarchy(directory);
            asset = ScriptableObject.CreateInstance<T>();
            AssetDatabase.CreateAsset(asset, assetPath);
            return true;
        }

        private static string ReadManifestPath(JObject manifest, params string[] segments)
        {
            JToken current = manifest;
            for (int i = 0; i < segments.Length; ++i)
                current = current?[segments[i]];

            return current?.Value<string>()?.Trim() ?? string.Empty;
        }

        private static bool IsManifestRelativePath(string path)
        {
            return !string.IsNullOrWhiteSpace(path)
                && !System.IO.Path.IsPathRooted(path)
                && !string.IsNullOrWhiteSpace(Core.Util.Path.ResolveSharedDataPath(path));
        }

        private static bool IsDomainKey(string value)
        {
            return !string.IsNullOrWhiteSpace(value)
                && value.IndexOfAny(System.IO.Path.GetInvalidFileNameChars()) < 0
                && value.IndexOf('/') < 0
                && value.IndexOf('\\') < 0;
        }
    }
}
