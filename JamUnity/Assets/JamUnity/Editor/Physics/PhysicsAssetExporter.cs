using System;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;
using UnityEngine;
using UnityEditor;

using JamUnity.Core.Data;
using JamUnity.Authoring.Physics;

using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Editor.Physics
{
    public static class PhysicsAssetExporter
    {
        private static readonly JsonSerializerSettings JsonSettings = new()
        {
            Formatting = Formatting.Indented,
            NullValueHandling = NullValueHandling.Ignore,
        };

        private sealed class Exporter : AssetExporterBase
        {
            public override bool TryExport(out string outputPath, out string errorMessage)
            {
                outputPath = string.Empty;
                errorMessage = string.Empty;

                if (TryResolveDefaultDatabase(out PhysicsAssetDatabase database, out _))
                    return TryExportAsset(database, out outputPath, out errorMessage);

                return TryExportCollected(PhysicsAssetDatabase.DefaultSharedDataPath, out outputPath, out errorMessage);
            }

            public override bool TryValidate(out string errorMessage)
            {
                if (!TryResolveDefaultDatabase(out PhysicsAssetDatabase database, out errorMessage))
                    return false;

                return TryBuildExportDto(database, out _, out errorMessage);
            }
        }

        [MenuItem("Tools/JamUnity/Export/Physics Asset")]
        public static void ExportPhysicsAsset()
        {
            if (!TryExportDefaultAsset(out string outputPath, out string errorMessage))
            {
                Debug.LogError(errorMessage);
                return;
            }

            Debug.Log($"Exported physics asset to {outputPath}");
        }

        [MenuItem("Tools/JamUnity/Validate/Physics Asset")]
        public static void ValidatePhysicsAsset()
        {
            if (!TryValidateDefaultAsset(out string errorMessage))
            {
                Debug.LogError(errorMessage);
                return;
            }

            Debug.Log("Physics asset validation passed.");
        }

        public static bool TryValidateDefaultAsset(out string errorMessage)
        {
            return new Exporter().TryValidate(out errorMessage);
        }

        public static bool TryExportDefaultAsset(out string outputPath, out string errorMessage)
        {
            return new Exporter().TryExport(out outputPath, out errorMessage);
        }

        public static bool TryExportAsset(PhysicsAssetDatabase database, out string outputPath, out string errorMessage)
        {
            outputPath = string.Empty;
            errorMessage = string.Empty;

            if (database == null)
            {
                errorMessage = "Physics asset database is null.";
                return false;
            }

            if (!TryBuildExportDto(database, out SharedGen.PhysicsAssetRootDto root, out errorMessage))
                return false;

            outputPath = JamUnity.Core.Util.Path.ResolveSharedDataPath(database.SharedDataPath);
            if (string.IsNullOrWhiteSpace(outputPath))
            {
                errorMessage = "Physics asset export path is empty.";
                return false;
            }

            Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
            File.WriteAllText(outputPath, JsonConvert.SerializeObject(root, JsonSettings));
            AssetDatabase.SaveAssets();
            return true;
        }

        public static bool TryBuildExportForValidation(PhysicsAssetDatabase database, out string errorMessage)
        {
            return TryBuildExportDto(database, out _, out errorMessage);
        }

        public static string GetDefaultOutputPath()
        {
            return JamUnity.Core.Util.Path.ResolveSharedDataPath(PhysicsAssetDatabase.DefaultSharedDataPath);
        }

        public static bool TryValidateCollected(out string errorMessage)
        {
            if (!TryResolveDefaultDatabase(out PhysicsAssetDatabase database, out errorMessage))
                return false;

            return TryBuildExportDto(database, out _, out errorMessage);
        }

        public static bool TryExportCollected(string configuredPath, out string outputPath, out string errorMessage)
        {
            outputPath = string.Empty;
            errorMessage = string.Empty;

            if (!TryResolveDefaultDatabase(out PhysicsAssetDatabase database, out errorMessage))
                return false;

            if (!TryBuildExportDto(database, out SharedGen.PhysicsAssetRootDto root, out errorMessage))
                return false;

            string relativeOrAbsolutePath = string.IsNullOrWhiteSpace(configuredPath) ? PhysicsAssetDatabase.DefaultSharedDataPath : configuredPath;
            outputPath = JamUnity.Core.Util.Path.ResolveSharedDataPath(relativeOrAbsolutePath);
            if (string.IsNullOrWhiteSpace(outputPath))
            {
                errorMessage = "Physics asset export path is empty.";
                return false;
            }

            Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
            File.WriteAllText(outputPath, JsonConvert.SerializeObject(root, JsonSettings));
            AssetDatabase.SaveAssets();
            return true;
        }

        public static string ResolveOutputPath(string projectRoot, string configuredPath)
        {
            string trimmed = configuredPath?.Trim() ?? string.Empty;
            if (string.IsNullOrWhiteSpace(trimmed))
                return string.Empty;

            return JamUnity.Core.Util.Path.ResolveSharedDataPath(trimmed);
        }

        private static bool TryBuildExportDto(PhysicsAssetDatabase database, out SharedGen.PhysicsAssetRootDto root, out string errorMessage)
        {
            root = new SharedGen.PhysicsAssetRootDto
            {
                version = database != null ? database.Version : 1
            };
            var errors = new List<string>();

            if (database == null)
            {
                errorMessage = "Physics asset database is null.";
                return false;
            }

            database.SortAndClean();

            CollectNamedAssets(database.Materials, root.materials, errors, static asset => asset.ToDto());
            CollectNamedAssets(database.Meshes, root.meshes, errors, static asset => asset.ToDto());
            CollectNamedAssets(database.Shapes, root.shapes, errors, static asset => asset.ToDto());
            CollectNamedAssets(database.DynamicBodies, root.dynBodies, errors, static asset => asset.ToDto());
            CollectNamedAssets(database.CctBodies, root.cctBodies, errors, static asset => asset.ToDto());
            CollectNamedAssets(database.CharacterMoveConfigs, root.charMoveConfigs, errors, static asset => asset.ToDto());
            CollectNamedAssets(database.KinematicDriverConfigs, root.kinematicDriverConfigs, errors, static asset => asset.ToDto());
            CollectNamedAssets(database.ProjectileConfigs, root.projectileConfigs, errors, static asset => asset.ToDto());
            CollectNamedAssets(database.PhysicsArchetypes, root.archetypes, errors, static asset => asset.ToDto());
            ValidatePhysicsArchetypes(database.PhysicsArchetypes, root, errors);

            errorMessage = FormatErrors(errors);
            return errors.Count == 0;
        }

        private static bool TryResolveDefaultDatabase(out PhysicsAssetDatabase asset, out string errorMessage)
        {
            asset = Selection.activeObject as PhysicsAssetDatabase;
            if (asset != null)
            {
                errorMessage = string.Empty;
                return true;
            }

            string[] guids = AssetDatabase.FindAssets("t:PhysicsAssetDatabase", new[] { PhysicsAssetDatabase.PhysicsDataRoot });
            if (guids.Length == 0)
            {
                errorMessage = $"No PhysicsAssetDatabase found under '{PhysicsAssetDatabase.PhysicsDataRoot}'.";
                return false;
            }

            if (guids.Length > 1)
            {
                errorMessage = $"Multiple PhysicsAssetDatabase assets found under '{PhysicsAssetDatabase.PhysicsDataRoot}'. Select one explicitly.";
                return false;
            }

            string assetPath = AssetDatabase.GUIDToAssetPath(guids[0]);
            asset = AssetDatabase.LoadAssetAtPath<PhysicsAssetDatabase>(assetPath);
            if (asset == null)
            {
                errorMessage = $"Failed to load PhysicsAssetDatabase at '{assetPath}'.";
                return false;
            }

            errorMessage = string.Empty;
            return true;
        }

        private static void CollectNamedAssets<TAsset, TDto>(IReadOnlyList<TAsset> source, Dictionary<string, TDto> output, List<string> errors, Func<TAsset, TDto> toDto)
            where TAsset : UnityEngine.Object, IAssetData
            where TDto : class
        {
            output.Clear();
            if (source == null)
                return;

            for (int i = 0; i < source.Count; ++i)
            {
                TAsset asset = source[i];
                if (asset == null)
                    continue;

                string assetName = NormalizeName(asset.AssetName);
                if (string.IsNullOrWhiteSpace(assetName))
                {
                    errors.Add($"{typeof(TAsset).Name} has empty name.");
                    continue;
                }

                if (output.ContainsKey(assetName))
                {
                    errors.Add($"duplicate {typeof(TAsset).Name} name '{assetName}'.");
                    continue;
                }

                TDto dto = toDto(asset);
                if (dto == null)
                {
                    errors.Add($"[{assetName}] failed to build dto from {typeof(TAsset).Name}.");
                    continue;
                }

                output.Add(assetName, dto);
            }
        }

        private static string NormalizeName(string value)
        {
            return AssetExporterBase.NormalizeName(value);
        }

        private static void ValidatePhysicsArchetypes(IReadOnlyList<PhysicsArchetypeData> physicsArchetypes, SharedGen.PhysicsAssetRootDto root, List<string> errors)
        {
            for (int i = 0; i < physicsArchetypes.Count; ++i)
            {
                PhysicsArchetypeData physicsArchetype = physicsArchetypes[i];
                if (physicsArchetype == null)
                    continue;

                string physicsArchetypeName = NormalizeName(physicsArchetype.AssetName);
                if (string.IsNullOrWhiteSpace(physicsArchetypeName))
                    continue;

                if (physicsArchetype.BodyType == eBodyType.RigidBody)
                {
                    ValidateShapeRefs(physicsArchetypeName, "rigid shape", physicsArchetype.RigidShapes, root.shapes, errors);
                    if (physicsArchetype.RigidShapes == null || physicsArchetype.RigidShapes.Count == 0)
                        errors.Add($"[physicsArchetype:{physicsArchetypeName}] rigid body requires at least one shape.");

                    ValidateNamedRef(physicsArchetypeName, "dynamic body", physicsArchetype.DynamicBody, root.dynBodies, errors, required: false);
                    switch (physicsArchetype.BehaviorKind)
                    {
                        case eRigidBehaviorKind.KinematicDriver:
                            ValidateNamedRef(physicsArchetypeName, "kinematic behavior config", physicsArchetype.KinematicBehavior, root.kinematicDriverConfigs, errors, required: true);
                            break;
                        case eRigidBehaviorKind.Projectile:
                            ValidateNamedRef(physicsArchetypeName, "projectile behavior config", physicsArchetype.ProjectileBehavior, root.projectileConfigs, errors, required: true);
                            break;
                    }
                }
                else
                {
                    ValidateNamedRef(physicsArchetypeName, "cct", physicsArchetype.CctBody, root.cctBodies, errors, required: true);
                    ValidateShapeRefs(physicsArchetypeName, "hitbox", physicsArchetype.Hitboxes, root.shapes, errors);
                    ValidateNamedRef(physicsArchetypeName, "move config", physicsArchetype.MoveConfig, root.charMoveConfigs, errors, required: true);
                }
            }
        }

        private static void ValidateShapeRefs(string physicsArchetypeName, string label, IReadOnlyList<ShapeData> refs, Dictionary<string, SharedGen.ShapeDto> lookup, List<string> errors)
        {
            if (refs == null)
                return;

            for (int i = 0; i < refs.Count; ++i)
            {
                ShapeData shape = refs[i];
                if (shape == null)
                {
                    errors.Add($"[physicsArchetype:{physicsArchetypeName}] {label} ref is null.");
                    continue;
                }

                string shapeName = NormalizeName(shape.AssetName);
                if (string.IsNullOrWhiteSpace(shapeName) || !lookup.ContainsKey(shapeName))
                    errors.Add($"[physicsArchetype:{physicsArchetypeName}] unknown {label} '{shapeName}'.");
            }
        }

        private static void ValidateNamedRef<TAsset, TDto>(string physicsArchetypeName, string label, TAsset asset, Dictionary<string, TDto> lookup, List<string> errors, bool required)
            where TAsset : UnityEngine.Object, IAssetData
            where TDto : class
        {
            string referencedAssetName = NormalizeName(asset != null ? asset.AssetName : string.Empty);
            if (string.IsNullOrWhiteSpace(referencedAssetName))
            {
                if (required)
                    errors.Add($"[physicsArchetype:{physicsArchetypeName}] unknown {label} '{referencedAssetName}'.");
                return;
            }

            if (!lookup.ContainsKey(referencedAssetName))
                errors.Add($"[physicsArchetype:{physicsArchetypeName}] unknown {label} '{referencedAssetName}'.");
        }

        private static string FormatErrors(List<string> errors)
        {
            if (errors.Count == 0)
                return string.Empty;

            return "Physics asset export failed.\n- " + string.Join("\n- ", errors);
        }
    }
} // namespace JamUnity.Editor.Physics
