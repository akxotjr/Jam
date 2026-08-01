using System;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;
using UnityEngine;
using UnityEditor;

using JamUnity.Authoring.Actor;

using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Editor.Actor
{
    public static class ActorArchetypeDatabaseExporter
    {
        [MenuItem("Tools/JamUnity/Export/Actor Archetypes")]
        public static void ExportActorArchetypesMenu()
        {
            ExportDefaultDatabase();
        }

        [MenuItem("Tools/JamUnity/Export/Actor Archetype Database")]
        public static void ExportDefaultDatabase()
        {
            if (!TryExport(out string message))
                Debug.LogError(message);
        }

        [MenuItem("Tools/JamUnity/Validate/Actor Archetypes")]
        public static void ValidateActorArchetypesMenu()
        {
            ValidateDefaultDatabase();
        }

        [MenuItem("Tools/JamUnity/Validate/Actor Archetype Database Export")]
        public static void ValidateDefaultDatabase()
        {
            if (TryValidate(out string message))
            {
                Debug.Log(message);
                return;
            }

            Debug.LogError(message);
        }

        public static bool TryExport(out string message)
        {
            ActorArchetypeDatabase database = ActorArchetypeDatabaseImporter.GetSelectedOrDefaultDatabase();
            if (database == null)
            {
                message = $"ActorArchetypeDatabase asset not found at '{ActorArchetypeDatabase.DatabaseAssetPath}'.";
                return false;
            }

            return TryExport(database, out message);
        }

        public static bool TryExport(ActorArchetypeDatabase database, out string message)
        {
            message = string.Empty;
            if (database == null)
            {
                message = "ActorArchetypeDatabase is null.";
                return false;
            }

            if (!database.TryBuildSharedRows(out Dictionary<string, SharedGen.ActorArchetypeDto> rows, out List<string> errors))
            {
                message = FormatMessage("Failed to write actor archetype database to shared.", errors);
                return false;
            }

            try
            {
                string outputPath = ResolveSharedDataPath(database.SharedDataPath);
                string directory = Path.GetDirectoryName(outputPath);
                if (!string.IsNullOrWhiteSpace(directory) && !Directory.Exists(directory))
                    Directory.CreateDirectory(directory);

                SharedGen.ActorArchetypesRootDto root = new()
                {
                    version = database.SharedDataVersion > 0 ? database.SharedDataVersion : 1,
                    archetypes = rows
                };

                string json = JsonConvert.SerializeObject(root, Formatting.Indented);
                File.WriteAllText(outputPath, json);
                message = $"Wrote {rows.Count} actor archetypes to {outputPath}.";
                return true;
            }
            catch (Exception e)
            {
                message = $"Failed to save actor archetypes json: {e.Message}";
                return false;
            }
        }

        public static bool TryValidate(out string message)
        {
            ActorArchetypeDatabase database = ActorArchetypeDatabaseImporter.GetSelectedOrDefaultDatabase();
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
            database.TryValidateEntries(out List<string> sharedErrors);
            errors.AddRange(sharedErrors);
            if (!database.TryBuildSharedRows(out Dictionary<string, SharedGen.ActorArchetypeDto> _, out List<string> buildErrors))
            {
                errors.AddRange(buildErrors);
            }

            if (errors.Count == 0)
            {
                message = "Actor archetype database export validation passed.";
                return true;
            }

            message = FormatMessage("Actor archetype database export validation failed.", errors);
            return false;
        }

        private static string ResolveSharedDataPath(string configuredPath)
        {
            return JamUnity.Core.Util.Path.ResolveSharedDataPath(configuredPath);
        }

        private static string FormatMessage(string title, List<string> errors)
        {
            return AssetEditorUtil.FormatMessage(title, errors);
        }
    }
    
} // namespace JamUnity.Editor.Actor
