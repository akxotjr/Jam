using System.IO;
using UnityEngine;

namespace JamUnity.Core.Util
{
    public static class Path
    {
        // All shared-data file paths are relative to this manifest's parent directory.
        public const string SharedDataManifestPath  = "C:/Users/akxotjr/GameWorkSpace/Jam-dev/SharedData/shared_data_manifest.json";
        public const string SharedRoot              = "../SharedData";
        public const string SharedSchemaRoot        = "Schema";
        public const string PhysicsAssetPath        = "physics_asset.json";
        public const string WorldArchetypesPath     = "world_archetypes.json";
        public const string WorldTemplatesPath      = "world_templates.json";
        public const string ActorArchetypesPath     = "actor_archetypes.json";
        public const string M1AssetRoot             = "Assets/M1";
        public const string GeneratedAssetRoot      = M1AssetRoot + "/Generated/Assets";
        public const string ContentAssetRoot        = M1AssetRoot + "/Contents";
        public const string ActorContentAssetRoot   = ContentAssetRoot + "/Actors";
        public const string WorldContentAssetRoot   = ContentAssetRoot + "/Worlds";
        public const string PresentationAssetRoot   = M1AssetRoot + "/Presentation";
        public const string ActorPresentationCatalogAssetPath = PresentationAssetRoot + "/ActorPresentationCatalog.asset";
        public const string WorldPresentationDatabaseAssetPath = PresentationAssetRoot + "/WorldPresentationDatabase.asset";

        public static string ResolveProjectPath(string configuredPath)
        {
            string trimmed = configuredPath?.Trim() ?? string.Empty;
            if (string.IsNullOrWhiteSpace(trimmed))
                return string.Empty;

            string projectRoot = Directory.GetParent(Application.dataPath)!.FullName;
            return System.IO.Path.IsPathRooted(trimmed)
                ? System.IO.Path.GetFullPath(trimmed)
                : System.IO.Path.GetFullPath(System.IO.Path.Combine(projectRoot, trimmed));
        }

        public static string ResolveSharedDataPath(string manifestRelativePath)
        {
            string relativePath = manifestRelativePath?.Trim() ?? string.Empty;
            if (string.IsNullOrWhiteSpace(relativePath) || System.IO.Path.IsPathRooted(relativePath))
                return string.Empty;

            string manifestPath = SharedDataManifestPath;
            string sharedDataRoot = System.IO.Path.GetDirectoryName(manifestPath);
            if (string.IsNullOrWhiteSpace(sharedDataRoot))
                return string.Empty;

            string resolvedRoot = System.IO.Path.GetFullPath(sharedDataRoot);
            string resolvedPath = System.IO.Path.GetFullPath(System.IO.Path.Combine(resolvedRoot, relativePath));
            string rootWithSeparator = resolvedRoot.TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar)
                + System.IO.Path.DirectorySeparatorChar;
            return resolvedPath.StartsWith(rootWithSeparator, System.StringComparison.OrdinalIgnoreCase)
                ? resolvedPath
                : string.Empty;
        }
    }
}
