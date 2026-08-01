using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

using Newtonsoft.Json;

using JamUnity.Actor.Runtime;
using JamUnity.Authoring.Actor;
using JamUnity.Authoring.Physics;
using JamUnity.Editor.Physics;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Editor.Actor
{
    public static class ActorArchetypeDatabaseImporter
    {
        [MenuItem("Tools/JamUnity/Import/Actor Archetypes")]
        public static void ImportActorArchetypesMenu()
        {
            ImportDefaultDatabase();
        }

        [MenuItem("Tools/JamUnity/Import/Actor Archetype Database")]
        public static void ImportDefaultDatabase()
        {
            if (!TryImport(out string message))
                Debug.LogError(message);
        }

        [MenuItem("Tools/JamUnity/Validate/Actor Archetypes Import")]
        public static void ValidateActorArchetypesImportMenu()
        {
            ValidateDefaultDatabase();
        }

        [MenuItem("Tools/JamUnity/Validate/Actor Archetype Database")]
        public static void ValidateDefaultDatabase()
        {
            if (TryValidate(out string message))
            {
                Debug.Log("Actor archetype database validation passed.");
                return;
            }

            Debug.LogError(message);
        }

        public static bool TryImport(out string message)
        {
            ActorArchetypeDatabase database = GetOrCreateSelectedDatabase();
            if (database == null)
            {
                message = "Failed to resolve ActorArchetypeDatabase asset.";
                return false;
            }

            return TryImport(database, out message);
        }

        public static bool TryImport(ActorArchetypeDatabase database, out string message)
        {
            return TryImport(database, LoadPhysicsDatabases(), out message);
        }

        public static bool TryImport(ActorArchetypeDatabase database, PhysicsAssetDatabase physicsDatabase, out string message)
        {
            IReadOnlyList<PhysicsAssetDatabase> physicsDatabases = physicsDatabase != null
                ? new[] { physicsDatabase }
                : Array.Empty<PhysicsAssetDatabase>();
            return TryImport(database, physicsDatabases, out message);
        }

        public static bool TryImport(ActorArchetypeDatabase database, IReadOnlyList<PhysicsAssetDatabase> physicsDatabases, out string message)
        {
            message = string.Empty;
            if (database == null)
            {
                message = "ActorArchetypeDatabase is null.";
                return false;
            }

            if (!TryLoadRoot(database, out SharedGen.ActorArchetypesRootDto root, out string inputPath, out List<string> errors))
            {
                message = FormatMessage("Failed to sync actor archetype database from shared.", errors);
                return false;
            }

            List<ActorArchetypeData> synced = new(root.archetypes.Count);
            List<string> warnings = new();

            foreach (KeyValuePair<string, SharedGen.ActorArchetypeDto> pair in root.archetypes)
            {
                string archetypeName = pair.Key?.Trim() ?? string.Empty;
                SharedGen.ActorArchetypeDto sharedDto = pair.Value;
                if (!ValidateSharedEntry(archetypeName, sharedDto, warnings))
                    continue;

                PhysicsArchetypeData physicsArchetype = ResolvePhysicsArchetype(archetypeName, sharedDto, physicsDatabases, warnings);
                ActorArchetypeData asset = LoadOrCreateArchetypeAsset(database, archetypeName);
                if (asset == null)
                {
                    warnings.Add($"[{archetypeName}] ActorArchetypeData asset could not be created.");
                    continue;
                }

                asset.SetArchetypeName(archetypeName);
                asset.FromDto(sharedDto);
                asset.SetPhysicsArchetype(physicsArchetype);
                EditorUtility.SetDirty(asset);

                synced.Add(asset);
            }

            database.ApplySharedSync(root.version, synced);
            EditorUtility.SetDirty(database);
            AssetDatabase.SaveAssets();

            message = warnings.Count == 0
                ? $"Synced {synced.Count} actor archetypes from {inputPath}."
                : FormatMessage($"Synced {synced.Count} actor archetypes from {inputPath} with warnings.", warnings);
            return true;
        }

        public static bool TryValidate(out string message)
        {
            ActorArchetypeDatabase database = GetSelectedOrDefaultDatabase();
            if (database == null)
            {
                message = $"ActorArchetypeDatabase asset not found at '{ActorArchetypeDatabase.DatabaseAssetPath}'.";
                return false;
            }

            return TryValidate(database, out message);
        }

        public static bool TryValidate(ActorArchetypeDatabase database, out string message)
        {
            message = string.Empty;
            if (database == null)
            {
                message = "ActorArchetypeDatabase is null.";
                return false;
            }

            List<string> errors = new();
            ValidateSharedSync(database, errors);

            if (errors.Count == 0)
            {
                message = "Actor archetype database validation passed.";
                return true;
            }

            message = FormatMessage("Actor archetype database validation failed.", errors);
            return false;
        }

        public static ActorArchetypeDatabase GetSelectedOrDefaultDatabase()
        {
            if (Selection.activeObject is ActorArchetypeDatabase selected)
                return selected;

            return AssetDatabase.LoadAssetAtPath<ActorArchetypeDatabase>(ActorArchetypeDatabase.DatabaseAssetPath);
        }

        public static ActorArchetypeDatabase GetOrCreateSelectedDatabase()
        {
            ActorArchetypeDatabase database = GetSelectedOrDefaultDatabase();
            if (database != null)
                return database;

            string directory = Path.GetDirectoryName(ActorArchetypeDatabase.DatabaseAssetPath);
            if (!string.IsNullOrWhiteSpace(directory) && !AssetDatabase.IsValidFolder(directory))
                EnsureFolders(directory);

            database = ScriptableObject.CreateInstance<ActorArchetypeDatabase>();
            AssetDatabase.CreateAsset(database, ActorArchetypeDatabase.DatabaseAssetPath);
            AssetDatabase.SaveAssets();
            return database;
        }

        private static void ValidateSharedSync(ActorArchetypeDatabase database, List<string> errors)
        {
            if (!TryLoadRoot(database, out SharedGen.ActorArchetypesRootDto root, out _, out List<string> loadErrors))
            {
                errors.AddRange(loadErrors);
                return;
            }

            Dictionary<string, ActorArchetypeData> localByName = new(StringComparer.Ordinal);
            foreach (ActorArchetypeData archetype in database.Archetypes)
            {
                if (archetype == null)
                    continue;

                string archetypeName = archetype.AssetName?.Trim() ?? string.Empty;
                if (!string.IsNullOrWhiteSpace(archetypeName))
                    localByName[archetypeName] = archetype;
            }

            foreach (KeyValuePair<string, SharedGen.ActorArchetypeDto> pair in root.archetypes)
            {
                string archetypeName = pair.Key?.Trim() ?? string.Empty;
                SharedGen.ActorArchetypeDto sharedDto = pair.Value;
                if (!ValidateSharedEntry(archetypeName, sharedDto, errors))
                    continue;

                if (!localByName.TryGetValue(archetypeName, out ActorArchetypeData archetype) || archetype == null)
                {
                    errors.Add($"[{archetypeName}] exists in shared json but not in ActorArchetypeDatabase.");
                    continue;
                }

                SharedGen.ActorArchetypeDto localDto = archetype.ToDto();
                if (!IsSharedMatch(localDto, sharedDto))
                    errors.Add($"[{archetypeName}] local ActorArchetypeData does not match shared json row.");

                ResolvePhysicsArchetype(archetypeName, sharedDto, LoadPhysicsDatabases(), errors);
                localByName.Remove(archetypeName);
            }

            foreach (KeyValuePair<string, ActorArchetypeData> pair in localByName)
                errors.Add($"[{pair.Key}] exists in ActorArchetypeDatabase but not in shared json.");
        }

        private static bool TryLoadRoot(ActorArchetypeDatabase database, out SharedGen.ActorArchetypesRootDto root, out string inputPath, out List<string> errors)
        {
            root = null;
            errors = new List<string>();
            inputPath = ResolveSharedDataPath(database.SharedDataPath);

            if (!File.Exists(inputPath))
            {
                errors.Add($"actor_archetypes.json not found: {inputPath}");
                return false;
            }

            try
            {
                root = JsonConvert.DeserializeObject<SharedGen.ActorArchetypesRootDto>(File.ReadAllText(inputPath));
            }
            catch (Exception e)
            {
                errors.Add($"Failed to parse actor_archetypes.json: {e.Message}");
                return false;
            }

            if (root == null || root.archetypes == null)
            {
                errors.Add($"Failed to parse actor_archetypes.json: {inputPath}");
                return false;
            }

            return true;
        }

        private static ActorArchetypeData LoadOrCreateArchetypeAsset(ActorArchetypeDatabase database, string archetypeName)
        {
            string databasePath = AssetDatabase.GetAssetPath(database);
            string dataRoot = Path.Combine(Path.GetDirectoryName(databasePath) ?? string.Empty, "Archetypes").Replace('\\', '/');
            return AssetEditorUtil.LoadOrCreateNamedAsset<ActorArchetypeData>(dataRoot, archetypeName);
        }

        private static bool ValidateSharedEntry(string archetypeName, SharedGen.ActorArchetypeDto dto, List<string> warnings)
        {
            if (dto == null)
            {
                warnings.Add($"[{archetypeName}] entry is null.");
                return false;
            }

            if (string.IsNullOrWhiteSpace(archetypeName))
            {
                warnings.Add("Shared actor archetypes contains an empty map key.");
                return false;
            }

            return true;
        }

        private static PhysicsArchetypeData ResolvePhysicsArchetype(string archetypeName, SharedGen.ActorArchetypeDto dto, IReadOnlyList<PhysicsAssetDatabase> physicsDatabases, List<string> warnings)
        {
            string physicsArchetypeName = dto.physicsArchetype?.Trim() ?? string.Empty;
            if (string.IsNullOrWhiteSpace(physicsArchetypeName))
                return null;

            PhysicsArchetypeData physicsArchetype = ResolvePhysicsArchetypeByName(physicsArchetypeName, physicsDatabases);
            if (physicsArchetype == null)
            {
                warnings.Add($"[{archetypeName}] physics archetype '{physicsArchetypeName}' could not be resolved.");
                return null;
            }

            return physicsArchetype;
        }

        private static PhysicsArchetypeData ResolvePhysicsArchetypeByName(string physicsArchetypeName, IReadOnlyList<PhysicsAssetDatabase> physicsDatabases)
        {
			if (physicsDatabases == null)
				return null;

			for (int databaseIndex = 0; databaseIndex < physicsDatabases.Count; ++databaseIndex)
			{
				PhysicsAssetDatabase physicsDatabase = physicsDatabases[databaseIndex];
				if (physicsDatabase == null)
					continue;
				for (int i = 0; i < physicsDatabase.PhysicsArchetypes.Count; ++i)
				{
					PhysicsArchetypeData physicsArchetype = physicsDatabase.PhysicsArchetypes[i];
					if (physicsArchetype != null && string.Equals(physicsArchetype.AssetName?.Trim(), physicsArchetypeName, StringComparison.Ordinal))
						return physicsArchetype;
				}
			}

            return null;
        }

        private static IReadOnlyList<PhysicsAssetDatabase> LoadPhysicsDatabases()
        {
            string[] guids = AssetDatabase.FindAssets("t:PhysicsAssetDatabase", new[] { Core.Util.Path.GeneratedAssetRoot + "/Physics" });
            var databases = new List<PhysicsAssetDatabase>(guids.Length);
            for (int i = 0; i < guids.Length; ++i)
            {
                PhysicsAssetDatabase database = AssetDatabase.LoadAssetAtPath<PhysicsAssetDatabase>(AssetDatabase.GUIDToAssetPath(guids[i]));
                if (database != null)
                    databases.Add(database);
            }
            databases.Sort((lhs, rhs) => string.Equals(lhs.name, "Common", StringComparison.Ordinal) ? -1
                : string.Equals(rhs.name, "Common", StringComparison.Ordinal) ? 1
                : string.CompareOrdinal(lhs.name, rhs.name));
            return databases;
        }

        private static bool IsSharedMatch(SharedGen.ActorArchetypeDto lhs, SharedGen.ActorArchetypeDto rhs)
        {
            if (lhs == null || rhs == null)
                return false;

            return string.Equals(lhs.physicsArchetype?.Trim() ?? string.Empty, rhs.physicsArchetype?.Trim() ?? string.Empty, StringComparison.Ordinal)
                && lhs.spawnPolicy == rhs.spawnPolicy
                && lhs.allowReplication == rhs.allowReplication;
        }

        private static string ResolveSharedDataPath(string configuredPath)
        {
            return JamUnity.Core.Util.Path.ResolveSharedDataPath(configuredPath);
        }

        private static void EnsureFolders(string assetDirectory)
        {
            string[] segments = assetDirectory.Split(new[] { '/' }, StringSplitOptions.RemoveEmptyEntries);
            if (segments.Length == 0 || !string.Equals(segments[0], "Assets", StringComparison.Ordinal))
                throw new InvalidOperationException($"Asset path must live under Assets/: {assetDirectory}");

            string current = "Assets";
            for (int i = 1; i < segments.Length; ++i)
            {
                string next = $"{current}/{segments[i]}";
                if (!AssetDatabase.IsValidFolder(next))
                    AssetDatabase.CreateFolder(current, segments[i]);
                current = next;
            }
        }

        private static string FormatMessage(string title, List<string> errors)
        {
            var sb = new StringBuilder(title);
            for (int i = 0; i < errors.Count; ++i)
            {
                sb.AppendLine();
                sb.Append("- ").Append(errors[i]);
            }

            return sb.ToString();
        }
    }
} // namespace JamUnity.Editor.Actor
