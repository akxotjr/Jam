using UnityEditor;
using UnityEngine;
using System;
using System.Collections.Generic;
using System.IO;

using Newtonsoft.Json;

using JamUnity.Authoring.World;
using JamUnity.Authoring.Physics;

using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Editor.World
{
    public static class WorldArchetypeDatabaseImporter
    {
        private static readonly JsonSerializerSettings JsonSettings = new()
        {
            MissingMemberHandling = MissingMemberHandling.Ignore,
            NullValueHandling = NullValueHandling.Include,
        };

        private sealed class Importer : AssetImporterBase
        {
            private readonly WorldArchetypeDatabase database;
            private readonly WorldTemplateDatabase templateDatabase;
            private readonly IReadOnlyDictionary<string, PhysicsAssetDatabase> physicsAssetDatabases;

            public Importer(
                WorldArchetypeDatabase database,
                WorldTemplateDatabase templateDatabase = null,
                IReadOnlyDictionary<string, PhysicsAssetDatabase> physicsAssetDatabases = null)
            {
                this.database = database;
                this.templateDatabase = templateDatabase ?? AssetDatabase.LoadAssetAtPath<WorldTemplateDatabase>(WorldTemplateDatabase.DatabaseAssetPath);
                this.physicsAssetDatabases = physicsAssetDatabases ?? LoadPhysicsAssetDatabases();
            }

            public override bool TryImport(out string message)
            {
                return TryImportInternal(applyChanges: true, out message);
            }

            public override bool TryValidate(out string message)
            {
                return TryImportInternal(applyChanges: false, out message);
            }

            private bool TryImportInternal(bool applyChanges, out string message)
            {
                message = string.Empty;

                if (database == null)
                {
                    message = "WorldArchetypeDatabase asset is null.";
                    return false;
                }

                string configuredPath = database.WorldArchetypesAssetPath;
                string inputPath = JamUnity.Core.Util.Path.ResolveSharedDataPath(configuredPath);
                if (string.IsNullOrWhiteSpace(inputPath))
                {
                    message = "World archetypes asset path is empty.";
                    return false;
                }

                if (!File.Exists(inputPath))
                {
                    message = $"world_archetypes.json not found: {inputPath}";
                    return false;
                }

                SharedGen.WorldArchetypesRootDto root;
                try
                {
                    root = JsonConvert.DeserializeObject<SharedGen.WorldArchetypesRootDto>(File.ReadAllText(inputPath), JsonSettings);
                }
                catch (Exception ex)
                {
                    message = $"Failed to parse world_archetypes json '{inputPath}': {ex.Message}";
                    return false;
                }

                if (root == null || root.worldArchetypes == null)
                {
                    message = $"world_archetypes.json is empty or invalid: {inputPath}";
                    return false;
                }

                List<string> warnings = new();
                EnsureFolderHierarchy(WorldArchetypeDatabase.WorldArchetypeDataRoot);
                List<WorldArchetypeData> imported = new();
                Dictionary<string, WorldArchetypeData> seenNames = new(StringComparer.Ordinal);

                foreach (KeyValuePair<string, SharedGen.WorldArchetypeDto> pair in root.worldArchetypes)
                {
                    SharedGen.WorldArchetypeDto dto = pair.Value;
                    string archetypeName = pair.Key?.Trim() ?? string.Empty;
                    if (string.IsNullOrWhiteSpace(archetypeName))
                    {
                        warnings.Add("world_archetypes contains an empty map key.");
                        continue;
                    }

                    if (dto == null)
                    {
                        warnings.Add($"[{archetypeName}] value is null.");
                        continue;
                    }

                    if (seenNames.ContainsKey(archetypeName))
                    {
                        warnings.Add($"Duplicate world archetype name '{archetypeName}'.");
                        continue;
                    }

                    WorldArchetypeData asset = LoadOrCreateNamedAsset<WorldArchetypeData>(WorldArchetypeDatabase.WorldArchetypeDataRoot, archetypeName);
                    if (asset == null)
                        continue;

                    WorldTemplateData template = ResolveWorldTemplate(archetypeName, dto.templateName, warnings);
                    PhysicsAssetDatabase physicsDatabase = ResolvePhysicsAssetDatabase(archetypeName, dto.physicsAssetName, warnings);

                    asset.AssetName = archetypeName;
                    asset.FromDto(dto);
                    asset.SetResolvedReferences(template, physicsDatabase);
                    EditorUtility.SetDirty(asset);

                    seenNames.Add(archetypeName, asset);
                    imported.Add(asset);
                }

                if (applyChanges)
                {
                    database.SetVersion(root.version);
                    database.SetEntries(imported);
                    EditorUtility.SetDirty(database);
                    AssetDatabase.SaveAssets();
                    message = warnings.Count == 0
                        ? $"Imported {imported.Count} world archetypes from {inputPath}."
                        : FormatMessage($"Imported {imported.Count} world archetypes from {inputPath} with warnings.", warnings);
                    return true;
                }

                message = warnings.Count == 0
                    ? $"World archetype database import validation passed: {inputPath}"
                    : FormatMessage("World archetype database import validation completed with warnings.", warnings);
                return warnings.Count == 0;
            }

            private WorldTemplateData ResolveWorldTemplate(string archetypeName, string templateName, List<string> warnings)
            {
                string normalizedName = templateName?.Trim() ?? string.Empty;
                if (string.IsNullOrWhiteSpace(normalizedName))
                {
                    warnings.Add($"[{archetypeName}] template_name is empty.");
                    return null;
                }

                if (templateDatabase != null && templateDatabase.TryGetTemplate(normalizedName, out WorldTemplateData template))
                    return template;

                warnings.Add($"[{archetypeName}] template_name '{normalizedName}' could not be resolved in WorldTemplateDatabase.");
                return null;
            }

            private PhysicsAssetDatabase ResolvePhysicsAssetDatabase(string archetypeName, string databaseName, List<string> warnings)
            {
                string normalizedName = databaseName?.Trim() ?? string.Empty;
                if (string.IsNullOrWhiteSpace(normalizedName))
                    return null;

                if (physicsAssetDatabases.TryGetValue(normalizedName, out PhysicsAssetDatabase physicsDatabase) && physicsDatabase != null)
                    return physicsDatabase;

                warnings.Add($"[{archetypeName}] physics_asset_name '{normalizedName}' could not be resolved.");
                return null;
            }

            private static Dictionary<string, PhysicsAssetDatabase> LoadPhysicsAssetDatabases()
            {
                Dictionary<string, PhysicsAssetDatabase> result = new(StringComparer.Ordinal);
                string[] guids = AssetDatabase.FindAssets("t:PhysicsAssetDatabase", new[] { Core.Util.Path.GeneratedAssetRoot + "/Physics" });
                for (int i = 0; i < guids.Length; ++i)
                {
                    PhysicsAssetDatabase database = AssetDatabase.LoadAssetAtPath<PhysicsAssetDatabase>(AssetDatabase.GUIDToAssetPath(guids[i]));
                    if (database != null && !result.ContainsKey(database.name))
                        result.Add(database.name, database);
                }

                return result;
            }
        }

        [MenuItem("Tools/JamUnity/Import/World Archetype Database")]
        public static void ImportWorldArchetypeDatabaseMenu()
        {
            if (!TryImport(out string message))
                Debug.LogError(message);
        }

        [MenuItem("Tools/JamUnity/Validate/World Archetype Database Import")]
        public static void ValidateWorldArchetypeDatabaseMenu()
        {
            if (TryValidate(out string message))
            {
                Debug.Log(message);
                return;
            }

            Debug.LogError(message);
        }

        public static bool TryImport(out string message)
        {
            WorldArchetypeDatabase database = GetOrCreateSelectedDatabase();
            if (database == null)
            {
                message = "Failed to resolve WorldArchetypeDatabase asset.";
                return false;
            }

            return TryImport(database, out message);
        }

        public static bool TryValidate(out string message)
        {
            WorldArchetypeDatabase database = GetSelectedOrDefaultDatabase();
            if (database == null)
            {
                message = $"WorldArchetypeDatabase asset not found at '{WorldArchetypeDatabase.DatabaseAssetPath}'.";
                return false;
            }

            return TryValidate(database, out message);
        }

        public static bool TryImport(WorldArchetypeDatabase database, out string message)
        {
            return new Importer(database).TryImport(out message);
        }

        public static bool TryImport(
            WorldArchetypeDatabase database,
            WorldTemplateDatabase templateDatabase,
            IReadOnlyDictionary<string, PhysicsAssetDatabase> physicsAssetDatabases,
            out string message)
        {
            return new Importer(database, templateDatabase, physicsAssetDatabases).TryImport(out message);
        }

        public static bool TryValidate(WorldArchetypeDatabase database, out string message)
        {
            return new Importer(database).TryValidate(out message);
        }

        public static WorldArchetypeDatabase GetSelectedOrDefaultDatabase()
        {
            if (Selection.activeObject is WorldArchetypeDatabase selected)
                return selected;

            return AssetDatabase.LoadAssetAtPath<WorldArchetypeDatabase>(WorldArchetypeDatabase.DatabaseAssetPath);
        }

        public static WorldArchetypeDatabase GetOrCreateSelectedDatabase()
        {
            WorldArchetypeDatabase database = GetSelectedOrDefaultDatabase();
            if (database != null)
                return database;

            string directory = System.IO.Path.GetDirectoryName(WorldArchetypeDatabase.DatabaseAssetPath);
            AssetEditorUtil.EnsureFolderHierarchy(directory);
            database = ScriptableObject.CreateInstance<WorldArchetypeDatabase>();
            AssetDatabase.CreateAsset(database, WorldArchetypeDatabase.DatabaseAssetPath);
            AssetDatabase.SaveAssets();
            return database;
        }
    }
} // namespace JamUnity.Editor.World
