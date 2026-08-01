using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEngine;

using JamUnity.Core.Data;

namespace JamUnity.Authoring.Physics
{
        
    [CreateAssetMenu(menuName = "JamUnity/Physics Asset Database", fileName = "PhysicsAssetDatabase")]
    public sealed class PhysicsAssetDatabase : ScriptableObject, ISerializationCallbackReceiver
    {
        public const string DatabaseAssetPath = Core.Util.Path.GeneratedAssetRoot + "/Physics/Common/Common.asset";
        public const string PhysicsDataRoot = Core.Util.Path.GeneratedAssetRoot + "/Physics/Common";
        public const string DefaultSharedDataPath = "M1/Physics/Common.physics_asset.json";

        [SerializeField] private string sharedDataPath = DefaultSharedDataPath;
        [SerializeField] private int    version = 1;
        [SerializeField] private List<MaterialData>               materials               = new();
        [SerializeField] private List<MeshData>                   meshes                  = new();
        [SerializeField] private List<ShapeData>                  shapes                  = new();
        [SerializeField] private List<DynamicBodyData>            dynamicBodies           = new();
        [SerializeField] private List<CctBodyData>                cctBodies               = new();
        [SerializeField] private List<CharacterMoveConfigData>    characterMoveConfigs    = new();
        [SerializeField] private List<KinematicDriverConfigData>  kinematicDriverConfigs  = new();
        [SerializeField] private List<ProjectileConfigData>       projectileConfigs       = new();
        [SerializeField] private List<PhysicsArchetypeData>       physicsArchetypes       = new();

        [NonSerialized] private readonly Dictionary<ulong, MaterialData>                materialsByKey              = new();
        [NonSerialized] private readonly Dictionary<ulong, MeshData>                    meshesByKey                 = new();
        [NonSerialized] private readonly Dictionary<ulong, ShapeData>                   shapesByKey                 = new();
        [NonSerialized] private readonly Dictionary<ulong, DynamicBodyData>             dynamicBodiesByKey          = new();
        [NonSerialized] private readonly Dictionary<ulong, CctBodyData>                 cctBodiesByKey              = new();
        [NonSerialized] private readonly Dictionary<ulong, CharacterMoveConfigData>     characterMoveConfigsByKey   = new();
        [NonSerialized] private readonly Dictionary<ulong, KinematicDriverConfigData>   kinematicDriverConfigsByKey = new();
        [NonSerialized] private readonly Dictionary<ulong, ProjectileConfigData>        projectileConfigsByKey      = new();
        [NonSerialized] private readonly Dictionary<ulong, PhysicsArchetypeData>        physicsArchetypesByKey      = new();
        [NonSerialized] private readonly Dictionary<string, PhysicsArchetypeData>       physicsArchetypesByName     = new(StringComparer.Ordinal);
        [NonSerialized] private bool lookupBuilt;

        public string   SharedDataPath => sharedDataPath?.Trim() ?? string.Empty;
        public string   ResolvedSharedDataPath => Core.Util.Path.ResolveSharedDataPath(SharedDataPath);
        public int      Version => version > 0 ? version : 1;

        public void SetSharedDataPath(string value)
        {
            sharedDataPath = value?.Trim() ?? string.Empty;
        }
        public IReadOnlyList<MaterialData>                Materials => materials;
        public IReadOnlyList<MeshData>                    Meshes => meshes;
        public IReadOnlyList<ShapeData>                   Shapes => shapes;
        public IReadOnlyList<DynamicBodyData>             DynamicBodies => dynamicBodies;
        public IReadOnlyList<CctBodyData>                 CctBodies => cctBodies;
        public IReadOnlyList<CharacterMoveConfigData>     CharacterMoveConfigs => characterMoveConfigs;
        public IReadOnlyList<KinematicDriverConfigData>   KinematicDriverConfigs => kinematicDriverConfigs;
        public IReadOnlyList<ProjectileConfigData>        ProjectileConfigs => projectileConfigs;
        public IReadOnlyList<PhysicsArchetypeData>        PhysicsArchetypes => physicsArchetypes;

        public bool LoadFromJson(out string message)
        {
    #if UNITY_EDITOR
            foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type importerType = assembly.GetType("JamUnity.Editor.Physics.Definition.PhysicsAssetImporter");
                if (importerType == null)
                    continue;

                MethodInfo method = importerType.GetMethod("TryImport", BindingFlags.Public | BindingFlags.Static);
                if (method == null)
                    continue;

                try
                {
                    object[] args = { this, null };
                    bool success = (bool)method.Invoke(null, args);
                    message = args[1] as string ?? string.Empty;
                    return success;
                }
                catch (TargetInvocationException ex)
                {
                    Exception inner = ex.InnerException ?? ex;
                    message = $"Physics asset import failed: {inner}";
                    return false;
                }
                catch (Exception ex)
                {
                    message = $"Physics asset import failed: {ex}";
                    return false;
                }
            }

            message = "Physics asset importer is not available in this editor context.";
            return false;
    #else
            message = "Physics asset import is editor-only.";
            return false;
    #endif
        }

        public bool SaveToJson(out string outputPath, out string message)
        {
    #if UNITY_EDITOR
            foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type exporterType = assembly.GetType("JamUnity.Editor.Physics.Definition.PhysicsAssetExporter");
                if (exporterType == null)
                    continue;

                MethodInfo method = exporterType.GetMethod("TryExportAsset", BindingFlags.Public | BindingFlags.Static);
                if (method == null)
                    continue;

                try
                {
                    object[] args = { this, null, null };
                    bool success = (bool)method.Invoke(null, args);
                    outputPath = args[1] as string ?? string.Empty;
                    message = args[2] as string ?? string.Empty;
                    return success;
                }
                catch (TargetInvocationException ex)
                {
                    Exception inner = ex.InnerException ?? ex;
                    outputPath = string.Empty;
                    message = $"Physics asset export failed: {inner}";
                    return false;
                }
                catch (Exception ex)
                {
                    outputPath = string.Empty;
                    message = $"Physics asset export failed: {ex}";
                    return false;
                }
            }

            outputPath = string.Empty;
            message = "Physics asset exporter is not available in this editor context.";
            return false;
    #else
            outputPath = string.Empty;
            message = "Physics asset export is editor-only.";
            return false;
    #endif
        }

        public bool ValidateForExport(out string message)
        {
    #if UNITY_EDITOR
            foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type exporterType = assembly.GetType("JamUnity.Editor.Physics.Definition.PhysicsAssetExporter");
                if (exporterType == null)
                    continue;

                MethodInfo method = exporterType.GetMethod("TryBuildExportForValidation", BindingFlags.Public | BindingFlags.Static);
                if (method == null)
                    continue;

                try
                {
                    object[] args = { this, null };
                    bool success = (bool)method.Invoke(null, args);
                    message = args[1] as string ?? string.Empty;
                    return success;
                }
                catch (TargetInvocationException ex)
                {
                    Exception inner = ex.InnerException ?? ex;
                    message = $"Physics asset validation failed: {inner}";
                    return false;
                }
                catch (Exception ex)
                {
                    message = $"Physics asset validation failed: {ex}";
                    return false;
                }
            }

            message = "Physics asset exporter is not available in this editor context.";
            return false;
    #else
            message = "Physics asset validation is editor-only.";
            return false;
    #endif
        }

        public void SetVersion(int value)
        {
            version = value > 0 ? value : 1;
        }

        public bool TryValidateEntries(out List<string> errors)
        {
            EnsureLookupBuilt();
            errors = new List<string>();

            ValidateSection(materials, nameof(materials), errors);
            ValidateSection(meshes, nameof(meshes), errors);
            ValidateSection(shapes, nameof(shapes), errors);
            ValidateSection(dynamicBodies, nameof(dynamicBodies), errors);
            ValidateSection(cctBodies, nameof(cctBodies), errors);
            ValidateSection(characterMoveConfigs, nameof(characterMoveConfigs), errors);
            ValidateSection(kinematicDriverConfigs, nameof(kinematicDriverConfigs), errors);
            ValidateSection(projectileConfigs, nameof(projectileConfigs), errors);
            ValidateSection(physicsArchetypes, nameof(physicsArchetypes), errors);

            return errors.Count == 0;
        }

        public bool TryGetMaterial(ulong key, out MaterialData asset)
        {
            EnsureLookupBuilt();
            return TryGet(materialsByKey, key, out asset);
        }

        public bool TryGetMesh(ulong key, out MeshData asset)
        {
            EnsureLookupBuilt();
            return TryGet(meshesByKey, key, out asset);
        }

        public bool TryGetShape(ulong key, out ShapeData asset)
        {
            EnsureLookupBuilt();
            return TryGet(shapesByKey, key, out asset);
        }

        public bool TryGetDynamicBody(ulong key, out DynamicBodyData asset)
        {
            EnsureLookupBuilt();
            return TryGet(dynamicBodiesByKey, key, out asset);
        }

        public bool TryGetCctBody(ulong key, out CctBodyData asset)
        {
            EnsureLookupBuilt();
            return TryGet(cctBodiesByKey, key, out asset);
        }

        public bool TryGetMoveConfig(ulong key, out CharacterMoveConfigData asset)
        {
            EnsureLookupBuilt();
            return TryGet(characterMoveConfigsByKey, key, out asset);
        }

        public bool TryGetKinematicConfig(ulong key, out KinematicDriverConfigData asset)
        {
            EnsureLookupBuilt();
            return TryGet(kinematicDriverConfigsByKey, key, out asset);
        }

        public bool TryGetProjectileConfig(ulong key, out ProjectileConfigData asset)
        {
            EnsureLookupBuilt();
            return TryGet(projectileConfigsByKey, key, out asset);
        }

        public bool TryGetPhysicsArchetype(ulong key, out PhysicsArchetypeData asset)
        {
            EnsureLookupBuilt();
            return TryGet(physicsArchetypesByKey, key, out asset);
        }

        public bool TryGetPhysicsArchetype(string physicsArchetypeName, out PhysicsArchetypeData asset)
        {
            EnsureLookupBuilt();
            asset = null;
            string normalizedPhysicsArchetypeName = physicsArchetypeName?.Trim() ?? string.Empty;
            return !string.IsNullOrWhiteSpace(normalizedPhysicsArchetypeName)
                && physicsArchetypesByName.TryGetValue(normalizedPhysicsArchetypeName, out asset)
                && asset != null;
        }

        public void SetSharedAssets(
            IEnumerable<MaterialData> materialAssets,
            IEnumerable<MeshData> meshAssets,
            IEnumerable<ShapeData> shapeAssets,
            IEnumerable<DynamicBodyData> dynamicBodyAssets,
            IEnumerable<CctBodyData> cctBodyAssets,
            IEnumerable<CharacterMoveConfigData> moveConfigAssets,
            IEnumerable<KinematicDriverConfigData> kinematicConfigAssets,
            IEnumerable<ProjectileConfigData> projectileConfigAssets,
            IEnumerable<PhysicsArchetypeData> physicsArchetypeAssets)
        {
            ReplaceList(materials, materialAssets);
            ReplaceList(meshes, meshAssets);
            ReplaceList(shapes, shapeAssets);
            ReplaceList(dynamicBodies, dynamicBodyAssets);
            ReplaceList(cctBodies, cctBodyAssets);
            ReplaceList(characterMoveConfigs, moveConfigAssets);
            ReplaceList(kinematicDriverConfigs, kinematicConfigAssets);
            ReplaceList(projectileConfigs, projectileConfigAssets);
            ReplaceList(physicsArchetypes, physicsArchetypeAssets);
            SortAndClean();
        }

        public void RegisterSharedAsset(ScriptableObject asset)
        {
            switch (asset)
            {
                case MaterialData material:
                    AddUnique(materials, material);
                    break;
                case MeshData mesh:
                    AddUnique(meshes, mesh);
                    break;
                case ShapeData shape:
                    AddUnique(shapes, shape);
                    break;
                case DynamicBodyData dynamicBody:
                    AddUnique(dynamicBodies, dynamicBody);
                    break;
                case CctBodyData cctBody:
                    AddUnique(cctBodies, cctBody);
                    break;
                case CharacterMoveConfigData moveConfig:
                    AddUnique(characterMoveConfigs, moveConfig);
                    break;
                case KinematicDriverConfigData kinematicConfig:
                    AddUnique(kinematicDriverConfigs, kinematicConfig);
                    break;
                case ProjectileConfigData projectileConfig:
                    AddUnique(projectileConfigs, projectileConfig);
                    break;
                case PhysicsArchetypeData physicsArchetype:
                    AddUnique(physicsArchetypes, physicsArchetype);
                    break;
            }
        }

        public void SortAndClean()
        {
            SortAndClean(materials);
            SortAndClean(meshes);
            SortAndClean(shapes);
            SortAndClean(dynamicBodies);
            SortAndClean(cctBodies);
            SortAndClean(characterMoveConfigs);
            SortAndClean(kinematicDriverConfigs);
            SortAndClean(projectileConfigs);
            SortAndClean(physicsArchetypes);
            RebuildLookup();
        }

        public static ulong MakeHandleKey(string assetName)
        {
            if (string.IsNullOrWhiteSpace(assetName))
                return 0;

            return JamUnity.Core.Util.StableKey.MakeStableKey(assetName.Trim());
        }

        private void OnValidate()
        {
            version = Version;
            SortAndClean();
        }

        public void OnBeforeSerialize()
        {
        }

        public void OnAfterDeserialize()
        {
            RebuildLookup();
        }

        private static void ReplaceList<T>(List<T> target, IEnumerable<T> source) where T : UnityEngine.Object
        {
            target.Clear();
            if (source == null)
                return;

            foreach (T item in source)
                AddUnique(target, item);
        }

        private static void AddUnique<T>(List<T> target, T asset) where T : UnityEngine.Object
        {
            if (asset == null || target.Contains(asset))
                return;

            target.Add(asset);
        }

        private void EnsureLookupBuilt()
        {
            if (!lookupBuilt)
                RebuildLookup();
        }

        private void RebuildLookup()
        {
            materialsByKey.Clear();
            meshesByKey.Clear();
            shapesByKey.Clear();
            dynamicBodiesByKey.Clear();
            cctBodiesByKey.Clear();
            characterMoveConfigsByKey.Clear();
            kinematicDriverConfigsByKey.Clear();
            projectileConfigsByKey.Clear();
            physicsArchetypesByKey.Clear();
            physicsArchetypesByName.Clear();

            RegisterAll(materials, materialsByKey);
            RegisterAll(meshes, meshesByKey);
            RegisterAll(shapes, shapesByKey);
            RegisterAll(dynamicBodies, dynamicBodiesByKey);
            RegisterAll(cctBodies, cctBodiesByKey);
            RegisterAll(characterMoveConfigs, characterMoveConfigsByKey);
            RegisterAll(kinematicDriverConfigs, kinematicDriverConfigsByKey);
            RegisterAll(projectileConfigs, projectileConfigsByKey);
            RegisterAll(physicsArchetypes, physicsArchetypesByKey);
            RegisterByName(physicsArchetypes, physicsArchetypesByName);

            lookupBuilt = true;
        }

        private static void RegisterAll<T>(IReadOnlyList<T> source, Dictionary<ulong, T> output) where T : UnityEngine.Object, IAssetData
        {
            if (source == null)
                return;

            for (int i = 0; i < source.Count; ++i)
            {
                T asset = source[i];
                if (asset == null)
                    continue;

                ulong key = MakeHandleKey(asset.AssetName);
                if (key == 0 || output.ContainsKey(key))
                    continue;

                output.Add(key, asset);
            }
        }

        private static void RegisterByName<T>(IReadOnlyList<T> source, Dictionary<string, T> output) where T : UnityEngine.Object, IAssetData
        {
            if (source == null)
                return;

            for (int i = 0; i < source.Count; ++i)
            {
                T asset = source[i];
                if (asset == null)
                    continue;

                string assetName = asset.AssetName?.Trim() ?? string.Empty;
                if (string.IsNullOrWhiteSpace(assetName) || output.ContainsKey(assetName))
                    continue;

                output.Add(assetName, asset);
            }
        }

        private static bool TryGet<T>(Dictionary<ulong, T> map, ulong key, out T asset) where T : UnityEngine.Object
        {
            asset = null;
            return key != 0 && map.TryGetValue(key, out asset) && asset != null;
        }

        private static void ValidateSection<T>(IReadOnlyList<T> assets, string sectionName, List<string> errors) where T : UnityEngine.Object, IAssetData
        {
            if (assets == null)
                return;

            HashSet<string> seenNames = new(StringComparer.Ordinal);
            HashSet<ulong> seenKeys = new();

            for (int i = 0; i < assets.Count; ++i)
            {
                T asset = assets[i];
                if (asset == null)
                {
                    errors.Add($"[{sectionName}] entry #{i} is null.");
                    continue;
                }

                string assetName = asset.AssetName?.Trim() ?? string.Empty;
                if (string.IsNullOrWhiteSpace(assetName))
                {
                    errors.Add($"[{sectionName}] entry #{i} has empty AssetName.");
                    continue;
                }

                if (!seenNames.Add(assetName))
                    errors.Add($"[{sectionName}] duplicate AssetName '{assetName}'.");

                ulong key = MakeHandleKey(assetName);
                if (key == 0)
                {
                    errors.Add($"[{sectionName}] '{assetName}' has invalid derived handle key.");
                    continue;
                }

                if (!seenKeys.Add(key))
                    errors.Add($"[{sectionName}] duplicate derived handle key '{key}' for '{assetName}'.");
            }
        }

        private static void SortAndClean<T>(List<T> assets) where T : UnityEngine.Object, IAssetData
        {
            assets.RemoveAll(static asset => asset == null);
            assets.Sort(static (lhs, rhs) => string.CompareOrdinal(lhs.AssetName, rhs.AssetName));
        }
    }

} // namespace JamUnity.Authoring.Physics
