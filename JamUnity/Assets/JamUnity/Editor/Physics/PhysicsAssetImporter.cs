using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using Newtonsoft.Json;
using UnityEditor;
using UnityEngine;

using JamUnity.Core.Data;
using JamUnity.Authoring.Physics;

using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Editor.Physics
{
    public static class PhysicsAssetImporter
    {
        private sealed class Importer : AssetImporterBase
        {
            public readonly struct StoragePaths
            {
                public readonly string Root;
                public readonly string Materials;
                public readonly string Meshes;
                public readonly string Shapes;
                public readonly string Bodies;
                public readonly string MoveConfigs;
                public readonly string Behaviors;
                public readonly string Archetypes;

                public StoragePaths(PhysicsAssetDatabase database)
                {
                    Root = System.IO.Path.GetDirectoryName(AssetDatabase.GetAssetPath(database))?.Replace('\\', '/') ?? string.Empty;
                    Materials = Root + "/Materials";
                    Meshes = Root + "/Meshes";
                    Shapes = Root + "/Shapes";
                    Bodies = Root + "/Bodies";
                    MoveConfigs = Root + "/MoveConfigs";
                    Behaviors = Root + "/Behaviors";
                    Archetypes = Root + "/Archetypes";
                }
            }

            private readonly StoragePaths paths;

            public Importer(PhysicsAssetDatabase database)
            {
                paths = new StoragePaths(database);
            }

            public override bool TryImport(out string message)
            {
                if (!TryResolveDatabase(out PhysicsAssetDatabase database, out message))
                    return false;

                return TryImportDatabase(database, null, out message);
            }

            internal bool TryImportDatabase(PhysicsAssetDatabase database, PhysicsAssetDatabase includedDatabase, out string message)
            {
                message = string.Empty;

                if (database == null)
                {
                    message = "Physics asset database is null.";
                    return false;
                }

                string inputPath = database.ResolvedSharedDataPath;
                if (string.IsNullOrWhiteSpace(inputPath))
                {
                    message = "Physics asset path is empty.";
                    return false;
                }

                if (!File.Exists(inputPath))
                {
                    message = $"Physics asset json not found: {inputPath}";
                    return false;
                }

                SharedGen.PhysicsAssetRootDto root;
                try
                {
                    JsonSerializerSettings settings = new JsonSerializerSettings
                    {
                        MissingMemberHandling = MissingMemberHandling.Ignore,
                        NullValueHandling = NullValueHandling.Include,
                    };

                    root = JsonConvert.DeserializeObject<SharedGen.PhysicsAssetRootDto>(File.ReadAllText(inputPath), settings);
                }
                catch (Exception ex)
                {
                    message = $"Failed to parse physics asset json '{inputPath}': {ex.Message}";
                    return false;
                }

                if (root == null)
                {
                    message = $"Physics asset json is empty or invalid: {inputPath}";
                    return false;
                }

                if (!ValidateComposition(root.composition, includedDatabase != null, out message))
                    return false;

                ImportContext ctx = new();
                SeedIncludedAssets(ctx, includedDatabase);
                EnsureFolders(paths);

                Dictionary<string, MaterialData> existingMaterials = LoadExistingAssets<MaterialData>(paths.Materials);
                Dictionary<string, MeshData> existingMeshes = LoadExistingAssets<MeshData>(paths.Meshes);
                Dictionary<string, ShapeData> existingShapes = LoadExistingAssets<ShapeData>(paths.Shapes);
                Dictionary<string, DynamicBodyData> existingDynamicBodies = LoadExistingAssets<DynamicBodyData>(paths.Bodies);
                Dictionary<string, CctBodyData> existingCctBodies = LoadExistingAssets<CctBodyData>(paths.Bodies);
                Dictionary<string, CharacterMoveConfigData> existingMoveConfigs = LoadExistingAssets<CharacterMoveConfigData>(paths.MoveConfigs);
                Dictionary<string, KinematicDriverConfigData> existingKinematicConfigs = LoadExistingAssets<KinematicDriverConfigData>(paths.Behaviors);
                Dictionary<string, ProjectileConfigData> existingProjectileConfigs = LoadExistingAssets<ProjectileConfigData>(paths.Behaviors);
                Dictionary<string, PhysicsArchetypeData> existingPhysicsArchetypes = LoadExistingAssets<PhysicsArchetypeData>(paths.Archetypes);

                UpsertNamedAssets(root.materials, ctx.materials, assetName => LoadOrCreatePhysicsAsset(paths.Materials, assetName, existingMaterials), static (asset, assetName, dto) => { asset.AssetName = assetName; asset.FromDto(dto); });
                UpsertNamedAssets(root.meshes, ctx.meshes, assetName => LoadOrCreatePhysicsAsset(paths.Meshes, assetName, existingMeshes), static (asset, assetName, dto) => { asset.AssetName = assetName; asset.FromDto(dto); });
                UpsertNamedAssets(root.dynBodies, ctx.dynamicBodies, assetName => LoadOrCreatePhysicsAsset(paths.Bodies, assetName, existingDynamicBodies), static (asset, assetName, dto) => { asset.AssetName = assetName; asset.FromDto(dto); });
                UpsertNamedAssets(root.cctBodies, ctx.cctBodies, assetName => LoadOrCreatePhysicsAsset(paths.Bodies, assetName, existingCctBodies), static (asset, assetName, dto) => { asset.AssetName = assetName; asset.FromDto(dto); });
                UpsertNamedAssets(root.charMoveConfigs, ctx.moveConfigs, assetName => LoadOrCreatePhysicsAsset(paths.MoveConfigs, assetName, existingMoveConfigs), static (asset, assetName, dto) => { asset.AssetName = assetName; asset.FromDto(dto); });
                UpsertNamedAssets(root.kinematicDriverConfigs, ctx.kinematicConfigs, assetName => LoadOrCreatePhysicsAsset(paths.Behaviors, assetName, existingKinematicConfigs), static (asset, assetName, dto) => { asset.AssetName = assetName; asset.FromDto(dto); });
                UpsertNamedAssets(root.projectileConfigs, ctx.projectileConfigs, assetName => LoadOrCreatePhysicsAsset(paths.Behaviors, assetName, existingProjectileConfigs), static (asset, assetName, dto) => { asset.AssetName = assetName; asset.FromDto(dto); });
                UpsertNamedAssets(root.shapes, ctx.shapes, assetName => LoadOrCreatePhysicsAsset(paths.Shapes, assetName, existingShapes), static (asset, assetName, dto) => { asset.AssetName = assetName; asset.FromDto(dto); });
                UpsertNamedAssets(root.archetypes, ctx.physicsArchetypes, assetName => LoadOrCreatePhysicsAsset(paths.Archetypes, assetName, existingPhysicsArchetypes), static (asset, assetName, dto) => { asset.AssetName = assetName; asset.FromDto(dto); });

                ResolveShapeRefs(root.shapes, ctx);
                ResolveCctRefs(root.cctBodies, ctx);
                ResolveArchetypeRefs(root.archetypes, ctx);

                database.SetVersion(root.version);
                database.SetSharedAssets(ctx.materials.Values, ctx.meshes.Values, ctx.shapes.Values, ctx.dynamicBodies.Values, ctx.cctBodies.Values, ctx.moveConfigs.Values, ctx.kinematicConfigs.Values, ctx.projectileConfigs.Values, ctx.physicsArchetypes.Values);
                EditorUtility.SetDirty(database);

                AssetDatabase.SaveAssets();
                AssetDatabase.Refresh();

                int importedCount = ctx.materials.Count + ctx.meshes.Count + ctx.shapes.Count + ctx.dynamicBodies.Count + ctx.cctBodies.Count + ctx.moveConfigs.Count + ctx.kinematicConfigs.Count + ctx.projectileConfigs.Count + ctx.physicsArchetypes.Count;
                message = $"Imported physics shared cache from {inputPath} ({importedCount} assets)";
                if (ctx.warnings.Count > 0)
                    message += "\n- " + string.Join("\n- ", ctx.warnings);
                return true;
            }

            private static bool ValidateComposition(SharedGen.PhysicsAssetCompositionDto composition, bool hasIncludedDatabase, out string message)
            {
                if (composition == null)
                {
                    message = "Physics source requires composition metadata.";
                    return false;
                }

                if (!hasIncludedDatabase)
                {
                    if (composition.scope != SharedGen.ePhysicsAssetCompositionDtoScope.Common
                        || (composition.includes != null && composition.includes.Count != 0))
                    {
                        message = "Common physics source must use scope 'common' without includes.";
                        return false;
                    }
                    message = string.Empty;
                    return true;
                }

                if (composition.scope != SharedGen.ePhysicsAssetCompositionDtoScope.World
                    || composition.includes == null
                    || composition.includes.Count != 1
                    || !string.Equals(composition.includes[0], "Common", StringComparison.Ordinal))
                {
                    message = "World physics source must include exactly 'Common'.";
                    return false;
                }

                message = string.Empty;
                return true;
            }

            private static bool TryResolveDatabase(out PhysicsAssetDatabase database, out string message)
            {
                database = Selection.activeObject as PhysicsAssetDatabase;
                if (database != null)
                {
                    message = string.Empty;
                    return true;
                }

                string[] guids = AssetDatabase.FindAssets("t:PhysicsAssetDatabase", new[] { PhysicsAssetDatabase.PhysicsDataRoot });
                if (guids.Length != 1)
                {
                    message = guids.Length == 0
                        ? $"No PhysicsAssetDatabase found under '{PhysicsAssetDatabase.PhysicsDataRoot}'."
                        : $"Multiple PhysicsAssetDatabase assets found under '{PhysicsAssetDatabase.PhysicsDataRoot}'. Select one explicitly.";
                    return false;
                }

                string assetPath = AssetDatabase.GUIDToAssetPath(guids[0]);
                database = AssetDatabase.LoadAssetAtPath<PhysicsAssetDatabase>(assetPath);
                message = database != null ? string.Empty : $"Failed to load PhysicsAssetDatabase at '{assetPath}'.";
                return database != null;
            }

            private static TAsset LoadOrCreatePhysicsAsset<TAsset>(string folderPath, string assetName, Dictionary<string, TAsset> existing)
                where TAsset : ScriptableObject, IAssetData
            {
                TAsset asset = PhysicsDataEditorUtil.LoadOrRepairNamedAsset<TAsset>(folderPath, assetName);
                if (asset == null && (existing == null || !existing.TryGetValue(assetName, out asset) || asset == null))
                    asset = PhysicsDataEditorUtil.CreateDataAsset<TAsset>(folderPath, assetName);

                return asset;
            }
        }

        private sealed class ImportContext
        {
            public readonly List<string> warnings = new();
            public readonly Dictionary<string, MaterialData> materials = new(StringComparer.Ordinal);
            public readonly Dictionary<string, MeshData> meshes = new(StringComparer.Ordinal);
            public readonly Dictionary<string, ShapeData> shapes = new(StringComparer.Ordinal);
            public readonly Dictionary<string, DynamicBodyData> dynamicBodies = new(StringComparer.Ordinal);
            public readonly Dictionary<string, CctBodyData> cctBodies = new(StringComparer.Ordinal);
            public readonly Dictionary<string, CharacterMoveConfigData> moveConfigs = new(StringComparer.Ordinal);
            public readonly Dictionary<string, KinematicDriverConfigData> kinematicConfigs = new(StringComparer.Ordinal);
            public readonly Dictionary<string, ProjectileConfigData> projectileConfigs = new(StringComparer.Ordinal);
            public readonly Dictionary<string, PhysicsArchetypeData> physicsArchetypes = new(StringComparer.Ordinal);
        }

        public static bool TryImport(PhysicsAssetDatabase database, out string message)
        {
            return new Importer(database).TryImportDatabase(database, null, out message);
        }

        public static bool TryImport(PhysicsAssetDatabase database, PhysicsAssetDatabase includedDatabase, out string message)
        {
            return new Importer(database).TryImportDatabase(database, includedDatabase, out message);
        }

        private static void SeedIncludedAssets(ImportContext ctx, PhysicsAssetDatabase included)
        {
            if (included == null)
                return;

            AddIncluded(included.Materials, ctx.materials);
            AddIncluded(included.Meshes, ctx.meshes);
            AddIncluded(included.Shapes, ctx.shapes);
            AddIncluded(included.DynamicBodies, ctx.dynamicBodies);
            AddIncluded(included.CctBodies, ctx.cctBodies);
            AddIncluded(included.CharacterMoveConfigs, ctx.moveConfigs);
            AddIncluded(included.KinematicDriverConfigs, ctx.kinematicConfigs);
            AddIncluded(included.ProjectileConfigs, ctx.projectileConfigs);
            AddIncluded(included.PhysicsArchetypes, ctx.physicsArchetypes);
        }

        private static void AddIncluded<TAsset>(IReadOnlyList<TAsset> assets, Dictionary<string, TAsset> target)
            where TAsset : UnityEngine.Object, IAssetData
        {
            if (assets == null)
                return;
            foreach (TAsset asset in assets)
            {
                if (asset != null && !string.IsNullOrWhiteSpace(asset.AssetName))
                    target.Add(asset.AssetName, asset);
            }
        }

        private static void EnsureFolders(Importer.StoragePaths paths)
        {
            EnsureFolder(paths.Root);
            EnsureFolder(paths.Materials);
            EnsureFolder(paths.Meshes);
            EnsureFolder(paths.Shapes);
            EnsureFolder(paths.Bodies);
            EnsureFolder(paths.MoveConfigs);
            EnsureFolder(paths.Behaviors);
            EnsureFolder(paths.Archetypes);
        }

        private static void EnsureFolder(string folderPath)
        {
            AssetEditorUtil.EnsureFolderHierarchy(folderPath);
        }

        private static void ResolveShapeRefs(Dictionary<string, SharedGen.ShapeDto> source, ImportContext ctx)
        {
            if (source == null)
                return;

            foreach (KeyValuePair<string, SharedGen.ShapeDto> pair in source)
            {
                if (!ctx.shapes.TryGetValue(pair.Key, out ShapeData asset) || asset == null || pair.Value == null)
                    continue;

                MaterialData material = ResolveNamedRef(pair.Value.material, ctx.materials);
                MeshData mesh = ResolveNamedRef(pair.Value.mesh, ctx.meshes);
                SetObjectField(asset, "material", material);
                SetObjectField(asset, "mesh", mesh);
                EditorUtility.SetDirty(asset);

                if (!string.IsNullOrWhiteSpace(pair.Value.material) && material == null)
                    ctx.warnings.Add($"shape '{pair.Key}' material ref could not be resolved.");

                bool requiresMesh = pair.Value.type == SharedGen.eShapeDtoType.TriangleMesh || pair.Value.type == SharedGen.eShapeDtoType.ConvexMesh;
                if (requiresMesh && mesh == null)
                    ctx.warnings.Add($"shape '{pair.Key}' mesh ref could not be resolved.");
            }
        }

        private static void ResolveCctRefs(Dictionary<string, SharedGen.CctBodyDto> source, ImportContext ctx)
        {
            if (source == null)
                return;

            foreach (KeyValuePair<string, SharedGen.CctBodyDto> pair in source)
            {
                if (!ctx.cctBodies.TryGetValue(pair.Key, out CctBodyData asset) || asset == null || pair.Value == null)
                    continue;

                MaterialData material = ResolveNamedRef(pair.Value.material, ctx.materials);
                SetObjectField(asset, "material", material);
                EditorUtility.SetDirty(asset);

                if (!string.IsNullOrWhiteSpace(pair.Value.material) && material == null)
                    ctx.warnings.Add($"cct_body '{pair.Key}' material ref could not be resolved.");
            }
        }

        private static void ResolveArchetypeRefs(Dictionary<string, SharedGen.PhysicsArchetypeDto> source, ImportContext ctx)
        {
            if (source == null)
                return;

            foreach (KeyValuePair<string, SharedGen.PhysicsArchetypeDto> pair in source)
            {
                if (!ctx.physicsArchetypes.TryGetValue(pair.Key, out PhysicsArchetypeData asset) || asset == null || pair.Value == null)
                    continue;

                switch (pair.Value)
                {
                    case SharedGen.RigidPhysicsArchetypeDto rigid:
                        SetObjectField(asset, "rigidShapes", ResolveNamedRefList(rigid.body?.shapes, ctx.shapes));
                        SetObjectField(asset, "dynamicBody", ResolveNamedRef(rigid.body?.dynamic, ctx.dynamicBodies));
                        SetObjectField(asset, "kinematicBehavior", ResolveBehaviorRef(rigid.body?.behavior, SharedGen.eRigidBehaviorDtoKind.KinematicDriver, ctx.kinematicConfigs));
                        SetObjectField(asset, "projectileBehavior", ResolveBehaviorRef(rigid.body?.behavior, SharedGen.eRigidBehaviorDtoKind.Projectile, ctx.projectileConfigs));
                        break;

                    case SharedGen.CharacterPhysicsArchetypeDto character:
                        SetObjectField(asset, "cctBody", ResolveNamedRef(character.body?.cct, ctx.cctBodies));
                        SetObjectField(asset, "hitboxes", ResolveNamedRefList(character.body?.hitboxes, ctx.shapes));
                        SetObjectField(asset, "moveConfig", ResolveNamedRef(character.body?.moveConfig, ctx.moveConfigs));
                        break;
                }

                EditorUtility.SetDirty(asset);
            }
        }

        private static TAsset ResolveNamedRef<TAsset>(string referenceName, Dictionary<string, TAsset> map) where TAsset : UnityEngine.Object
        {
            string normalizedReferenceName = referenceName?.Trim() ?? string.Empty;
            if (string.IsNullOrWhiteSpace(normalizedReferenceName) || map == null)
                return null;

            return map.TryGetValue(normalizedReferenceName, out TAsset asset) ? asset : null;
        }

        private static List<TAsset> ResolveNamedRefList<TAsset>(IReadOnlyList<string> refs, Dictionary<string, TAsset> map) where TAsset : UnityEngine.Object
        {
            var result = new List<TAsset>();
            if (refs == null || map == null)
                return result;

            for (int i = 0; i < refs.Count; ++i)
            {
                TAsset asset = ResolveNamedRef(refs[i], map);
                if (asset != null)
                    result.Add(asset);
            }

            return result;
        }

        private static TAsset ResolveBehaviorRef<TAsset>(SharedGen.RigidBehaviorDto behavior, SharedGen.eRigidBehaviorDtoKind expectedKind, Dictionary<string, TAsset> map) where TAsset : UnityEngine.Object
        {
            if (behavior == null || behavior.kind != expectedKind)
                return null;

            return ResolveNamedRef(behavior.config, map);
        }

        private static void SetObjectField(UnityEngine.Object target, string fieldName, object value)
        {
            if (target == null)
                return;

            FieldInfo field = target.GetType().GetField(fieldName, BindingFlags.Instance | BindingFlags.NonPublic);
            field?.SetValue(target, value);
        }
    }
} // namespace JamUnity.Editor.Physics
