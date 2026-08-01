using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;

using Newtonsoft.Json;

using JamUnity.Authoring.World;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Editor.World
{
    public static class WorldTemplateAssetExporter
    {
        private static readonly JsonSerializerSettings JsonSettings = new()
        {
            Formatting = Formatting.Indented,
            NullValueHandling = NullValueHandling.Ignore,
        };

        private sealed class Exporter : AssetExporterBase
        {
            private readonly WorldTemplateDatabase database;

            public Exporter(WorldTemplateDatabase database)
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
                    errorMessage = "WorldTemplateDatabase asset is null.";
                    return false;
                }

                string configuredPath = database.WorldTemplateAssetPath;
                if (string.IsNullOrWhiteSpace(configuredPath))
                {
                    errorMessage = "World template asset path is empty.";
                    return false;
                }

                database.SortAndClean();

                SharedGen.WorldTemplatesRootDto root = new()
                {
                    version = database.Version
                };
                List<string> errors = new();
                CollectNamedAssets(database.Entries, root.worldTemplates = new Dictionary<string, SharedGen.WorldTemplateDto>(System.StringComparer.Ordinal), errors);

                if (errors.Count > 0)
                {
                    errorMessage = FormatMessage("World template export failed.", errors);
                    return false;
                }

                outputPath = JamUnity.Core.Util.Path.ResolveSharedDataPath(configuredPath);
                if (string.IsNullOrWhiteSpace(outputPath))
                {
                    errorMessage = "World template export path is empty.";
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

        [MenuItem("Tools/JamUnity/Export/World Template Database")]
        public static void ExportWorldTemplateDatabaseMenu()
        {
            if (!TryExport(out string outputPath, out string errorMessage))
                Debug.LogError(errorMessage);
            else
                Debug.Log($"Exported world templates to {outputPath}");
        }

        public static bool TryExport(out string outputPath, out string errorMessage)
        {
            WorldTemplateDatabase database = WorldTemplateAssetImporter.GetSelectedOrDefaultDatabase();
            if (database == null)
            {
                outputPath = string.Empty;
                errorMessage = "WorldTemplateDatabase asset is not assigned.";
                return false;
            }

            return TryExport(database, out outputPath, out errorMessage);
        }

        public static bool TryValidate(WorldTemplateDatabase database, out string errorMessage)
        {
            return new Exporter(database).TryValidate(out errorMessage);
        }

        public static bool TryExport(WorldTemplateDatabase database, out string outputPath, out string errorMessage)
        {
            return new Exporter(database).TryExport(out outputPath, out errorMessage);
        }
    }
} // namespace JamUnity.Editor.World
