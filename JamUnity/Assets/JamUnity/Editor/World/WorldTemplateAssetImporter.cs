using UnityEditor;
using UnityEngine;
using System;
using System.Collections.Generic;
using System.IO;

using Newtonsoft.Json;

using JamUnity.Authoring.World;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Editor.World
{
    public static class WorldTemplateAssetImporter
    {
        private static readonly JsonSerializerSettings JsonSettings = new()
        {
            MissingMemberHandling = MissingMemberHandling.Ignore,
            NullValueHandling = NullValueHandling.Include,
        };

        private sealed class Importer : AssetImporterBase
        {
            private readonly WorldTemplateDatabase database;

            public Importer(WorldTemplateDatabase database)
            {
                this.database = database;
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
                    message = "WorldTemplateDatabase asset is null.";
                    return false;
                }

                string configuredPath = database.WorldTemplateAssetPath;
                string inputPath = JamUnity.Core.Util.Path.ResolveSharedDataPath(configuredPath);
                if (string.IsNullOrWhiteSpace(inputPath))
                {
                    message = "World template asset path is empty.";
                    return false;
                }

                if (!File.Exists(inputPath))
                {
                    message = $"world_templates.json not found: {inputPath}";
                    return false;
                }

                SharedGen.WorldTemplatesRootDto root;
                try
                {
                    root = JsonConvert.DeserializeObject<SharedGen.WorldTemplatesRootDto>(File.ReadAllText(inputPath), JsonSettings);
                }
                catch (Exception ex)
                {
                    message = $"Failed to parse world_templates json '{inputPath}': {ex.Message}";
                    return false;
                }

                if (root == null || root.worldTemplates == null)
                {
                    message = $"world_templates.json is empty or invalid: {inputPath}";
                    return false;
                }

                List<string> warnings = new();
                EnsureFolderHierarchy(WorldTemplateDatabase.WorldTemplateDataRoot);
                Dictionary<string, WorldTemplateData> imported = new(StringComparer.Ordinal);

                UpsertNamedAssets(
                    root.worldTemplates,
                    imported,
                    templateName => LoadOrCreateNamedAsset<WorldTemplateData>(WorldTemplateDatabase.WorldTemplateDataRoot, templateName),
                    (asset, templateName, dto) =>
                    {
                        asset.AssetName = templateName;
                        asset.FromDto(dto ?? new SharedGen.WorldTemplateDto());
                    });

                if (applyChanges)
                {
                    database.SetVersion(root.version);
                    database.SetEntries(imported.Values);
                    EditorUtility.SetDirty(database);
                    AssetDatabase.SaveAssets();
                    message = warnings.Count == 0
                        ? $"Imported {imported.Count} world templates from {inputPath}."
                        : FormatMessage($"Imported {imported.Count} world templates from {inputPath} with warnings.", warnings);
                    return true;
                }

                message = warnings.Count == 0
                    ? $"World template database import validation passed: {inputPath}"
                    : FormatMessage("World template database import validation completed with warnings.", warnings);
                return warnings.Count == 0;
            }
        }

        [MenuItem("Tools/JamUnity/Import/World Template Database")]
        public static void ImportWorldTemplateDatabaseMenu()
        {
            if (!TryImport(out string message))
                Debug.LogError(message);
        }

        [MenuItem("Tools/JamUnity/Validate/World Template Database Import")]
        public static void ValidateWorldTemplateDatabaseMenu()
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
            WorldTemplateDatabase database = GetOrCreateSelectedDatabase();
            if (database == null)
            {
                message = "Failed to resolve WorldTemplateDatabase asset.";
                return false;
            }

            return TryImport(database, out message);
        }

        public static bool TryValidate(out string message)
        {
            WorldTemplateDatabase database = GetSelectedOrDefaultDatabase();
            if (database == null)
            {
                message = $"WorldTemplateDatabase asset not found at '{WorldTemplateDatabase.DatabaseAssetPath}'.";
                return false;
            }

            return TryValidate(database, out message);
        }

        public static bool TryImport(WorldTemplateDatabase database, out string message)
        {
            return new Importer(database).TryImport(out message);
        }

        public static bool TryValidate(WorldTemplateDatabase database, out string message)
        {
            return new Importer(database).TryValidate(out message);
        }

        public static WorldTemplateDatabase GetSelectedOrDefaultDatabase()
        {
            if (Selection.activeObject is WorldTemplateDatabase selected)
                return selected;

            return AssetDatabase.LoadAssetAtPath<WorldTemplateDatabase>(WorldTemplateDatabase.DatabaseAssetPath);
        }

        public static WorldTemplateDatabase GetOrCreateSelectedDatabase()
        {
            WorldTemplateDatabase database = GetSelectedOrDefaultDatabase();
            if (database != null)
                return database;

            string directory = System.IO.Path.GetDirectoryName(WorldTemplateDatabase.DatabaseAssetPath);
            AssetEditorUtil.EnsureFolderHierarchy(directory);
            database = ScriptableObject.CreateInstance<WorldTemplateDatabase>();
            AssetDatabase.CreateAsset(database, WorldTemplateDatabase.DatabaseAssetPath);
            AssetDatabase.SaveAssets();
            return database;
        }
    }
} // namespace JamUnity.Editor.World
