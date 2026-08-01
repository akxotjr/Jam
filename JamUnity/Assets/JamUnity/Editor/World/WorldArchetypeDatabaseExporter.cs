using UnityEditor;
using UnityEngine;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;

using JamUnity.Authoring.World;

using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Editor.World
{
    public static class WorldArchetypeDatabaseExporter
    {
        private static readonly JsonSerializerSettings JsonSettings = new()
        {
            Formatting = Formatting.Indented,
            NullValueHandling = NullValueHandling.Ignore,
        };
        private static WorldTemplateDatabase ResolveTemplateDatabase() => AssetDatabase.LoadAssetAtPath<WorldTemplateDatabase>(WorldTemplateDatabase.DatabaseAssetPath);

        private sealed class Exporter : AssetExporterBase
        {
            private readonly WorldArchetypeDatabase database;

            public Exporter(WorldArchetypeDatabase database)
            {
                this.database = database;
            }

            public override bool TryExport(out string outputPath, out string errorMessage)
            {
                return TryExportInternal(writeFile: true, out outputPath, out errorMessage);
            }

            public override bool TryValidate(out string errorMessage)
            {
                return TryExportInternal(writeFile: false, out _, out errorMessage);
            }

            private bool TryExportInternal(bool writeFile, out string outputPath, out string errorMessage)
            {
                outputPath = string.Empty;
                errorMessage = string.Empty;

                if (database == null)
                {
                    errorMessage = "WorldArchetypeDatabase asset is null.";
                    return false;
                }

                string configuredPath = database.WorldArchetypesAssetPath;
                if (string.IsNullOrWhiteSpace(configuredPath))
                {
                    errorMessage = "World archetypes asset path is empty.";
                    return false;
                }

                database.SortAndClean();

                SharedGen.WorldArchetypesRootDto root = new()
                {
                    version = database.Version,
                    worldArchetypes = new Dictionary<string, SharedGen.WorldArchetypeDto>()
                };
                List<string> errors = new();
                WorldTemplateDatabase templateDatabase = ResolveTemplateDatabase();

                IReadOnlyList<WorldArchetypeData> entries = database.Entries;
                for (int i = 0; i < entries.Count; ++i)
                {
                    WorldArchetypeData asset = entries[i];
                    if (asset == null)
                        continue;

                    string assetName = NormalizeName(asset.AssetName);
                    if (string.IsNullOrWhiteSpace(assetName))
                    {
                        errors.Add("WorldArchetypeData has empty name.");
                        continue;
                    }

                    SharedGen.WorldArchetypeDto dto = asset.ToDto();
                    if (dto == null)
                    {
                        errors.Add($"[{assetName}] failed to build world archetype dto.");
                        continue;
                    }

                    if (asset.WorldTemplate == null)
                        errors.Add($"[{assetName}] WorldTemplate reference is not assigned.");
                    else if (templateDatabase != null && !templateDatabase.TryGetTemplate(dto.templateName, out _))
                        errors.Add($"[{assetName}] WorldTemplate '{dto.templateName}' is not present in WorldTemplateDatabase.");

                    if (!string.IsNullOrWhiteSpace(dto.physicsAssetName) && asset.PhysicsAssetDatabase == null)
                        errors.Add($"[{assetName}] PhysicsAssetDatabase reference is not assigned.");
                    root.worldArchetypes[assetName] = dto;
                }

                if (errors.Count > 0)
                {
                    errorMessage = FormatMessage("World archetype export failed.", errors);
                    return false;
                }

                outputPath = JamUnity.Core.Util.Path.ResolveSharedDataPath(configuredPath);
                if (string.IsNullOrWhiteSpace(outputPath))
                {
                    errorMessage = "World archetype export path is empty.";
                    return false;
                }

                if (!writeFile)
                    return true;

                Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
                File.WriteAllText(outputPath, JsonConvert.SerializeObject(root, JsonSettings));
                EditorUtility.SetDirty(database);
                AssetDatabase.SaveAssets();
                return true;
            }
        }

        [MenuItem("Tools/JamUnity/Export/World Archetype Database")]
        public static void ExportWorldArchetypeDatabaseMenu()
        {
            if (!TryExport(out string outputPath, out string errorMessage))
                Debug.LogError(errorMessage);
            else
                Debug.Log($"Exported world archetypes to {outputPath}");
        }

        public static bool TryExport(out string outputPath, out string errorMessage)
        {
            WorldArchetypeDatabase database = WorldArchetypeDatabaseImporter.GetSelectedOrDefaultDatabase();
            if (database == null)
            {
                outputPath = string.Empty;
                errorMessage = "WorldArchetypeDatabase asset is not assigned.";
                return false;
            }

            return TryExport(database, out outputPath, out errorMessage);
        }

        public static bool TryValidate(WorldArchetypeDatabase database, out string errorMessage)
        {
            return new Exporter(database).TryValidate(out errorMessage);
        }

        public static bool TryExport(WorldArchetypeDatabase database, out string outputPath, out string errorMessage)
        {
            return new Exporter(database).TryExport(out outputPath, out errorMessage);
        }
    }
} // namespace JamUnity.Editor.World
