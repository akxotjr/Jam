using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;
using JamUnity.Core.Data;

namespace JamUnity.Editor
{
    internal interface IAssetImporter
    {
        bool TryImport(out string message);
        bool TryValidate(out string message);
    }

    internal interface IAssetExporter
    {
        bool TryExport(out string outputPath, out string errorMessage);
        bool TryValidate(out string errorMessage);
    }

    internal abstract class AssetImporterBase : IAssetImporter
    {
        public abstract bool TryImport(out string message);

        public virtual bool TryValidate(out string message)
        {
            message = string.Empty;
            return true;
        }

        protected static void EnsureFolderHierarchy(string folderPath)
        {
            AssetEditorUtil.EnsureFolderHierarchy(folderPath);
        }

        protected static string FormatMessage(string title, List<string> messages)
        {
            return AssetEditorUtil.FormatMessage(title, messages);
        }

        protected static TAsset LoadOrCreateNamedAsset<TAsset>(string folderPath, string assetName)
            where TAsset : ScriptableObject
        {
            return AssetEditorUtil.LoadOrCreateNamedAsset<TAsset>(folderPath, assetName);
        }

        protected static Dictionary<string, TAsset> LoadExistingAssets<TAsset>(string folderPath)
            where TAsset : ScriptableObject, IAssetData
        {
            Dictionary<string, TAsset> result = new(StringComparer.Ordinal);
            string[] guids = AssetDatabase.FindAssets($"t:{typeof(TAsset).Name}", new[] { folderPath });
            for (int i = 0; i < guids.Length; ++i)
            {
                string assetPath = AssetDatabase.GUIDToAssetPath(guids[i]);
                TAsset asset = AssetDatabase.LoadAssetAtPath<TAsset>(assetPath);
                if (asset != null)
                {
                    string assetName = AssetEditorUtil.NormalizeName(asset.AssetName);
                    if (!string.IsNullOrWhiteSpace(assetName))
                        result[assetName] = asset;
                }
            }

            return result;
        }

        protected static void UpsertNamedAssets<TAsset, TDto>(
            IReadOnlyDictionary<string, TDto> source,
            Dictionary<string, TAsset> output,
            Func<string, TAsset> loadOrCreateAsset,
            Action<TAsset, string, TDto> apply)
            where TAsset : ScriptableObject, IAssetData<TDto>
        {
            if (source == null)
                return;

            foreach (KeyValuePair<string, TDto> pair in source)
            {
                string normalizedAssetName = AssetEditorUtil.NormalizeName(pair.Key);
                if (string.IsNullOrWhiteSpace(normalizedAssetName))
                    continue;

                TAsset asset = loadOrCreateAsset(normalizedAssetName);
                if (asset == null)
                    continue;

                apply(asset, normalizedAssetName, pair.Value);
                EditorUtility.SetDirty(asset);
                output[normalizedAssetName] = asset;
            }
        }
    }

    internal abstract class AssetExporterBase : IAssetExporter
    {
        public abstract bool TryExport(out string outputPath, out string errorMessage);
        public abstract bool TryValidate(out string errorMessage);

        internal static void CollectNamedAssets<TAsset, TDto>(
            IReadOnlyList<TAsset> source,
            Dictionary<string, TDto> output,
            List<string> errors)
            where TAsset : UnityEngine.Object, IAssetData<TDto>
        {
            output.Clear();

            for (int i = 0; i < source.Count; ++i)
            {
                TAsset asset = source[i];
                if (asset == null)
                    continue;

                string assetName = AssetEditorUtil.NormalizeName(asset.AssetName);
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

                TDto dto = asset.ToDto();
                output.Add(assetName, dto);
            }
        }

        internal static string ResolveOutputPath(string projectRoot, string configuredPath)
        {
            return AssetEditorUtil.ResolveProjectPath(projectRoot, configuredPath);
        }

        internal static string FormatMessage(string title, List<string> messages)
        {
            return AssetEditorUtil.FormatMessage(title, messages);
        }

        internal static string NormalizeName(string value)
        {
            return AssetEditorUtil.NormalizeName(value);
        }
    }

    internal static class AssetEditorUtil
    {
        public static void EnsureFolderHierarchy(string folderPath)
        {
            string[] segments = folderPath.Split(new[] { '/' }, StringSplitOptions.RemoveEmptyEntries);
            if (segments.Length == 0)
                return;

            string current = segments[0];
            for (int i = 1; i < segments.Length; ++i)
            {
                string next = $"{current}/{segments[i]}";
                if (!AssetDatabase.IsValidFolder(next))
                    AssetDatabase.CreateFolder(current, segments[i]);
                current = next;
            }
        }

        public static string SanitizeFileName(string value)
        {
            string text = string.IsNullOrWhiteSpace(value) ? "AssetData" : value.Trim();
            foreach (char c in Path.GetInvalidFileNameChars())
                text = text.Replace(c, '_');
            return text;
        }

        public static TAsset LoadOrCreateNamedAsset<TAsset>(string folderPath, string assetName)
            where TAsset : ScriptableObject
        {
            EnsureFolderHierarchy(folderPath);

            string safeName = SanitizeFileName(assetName);
            string assetPath = $"{folderPath}/{safeName}.asset";
            TAsset asset = AssetDatabase.LoadAssetAtPath<TAsset>(assetPath);
            if (asset != null)
                return asset;

            asset = ScriptableObject.CreateInstance<TAsset>();
            AssetDatabase.CreateAsset(asset, assetPath);
            if (asset is IAssetData namedAsset)
                namedAsset.AssetName = assetName;

            return asset;
        }

        public static string NormalizeName(string value)
        {
            return value?.Trim() ?? string.Empty;
        }

        public static string ResolveProjectPath(string projectRoot, string configuredPath)
        {
            string trimmed = configuredPath?.Trim() ?? string.Empty;
            if (string.IsNullOrWhiteSpace(trimmed))
                return string.Empty;

            if (Path.IsPathRooted(trimmed))
                return Path.GetFullPath(trimmed);

            return Path.GetFullPath(Path.Combine(projectRoot, trimmed));
        }

        public static string FormatMessage(string title, List<string> messages)
        {
            if (messages == null || messages.Count == 0)
                return title;

            return $"{title}\n- {string.Join("\n- ", messages)}";
        }
    }
} // namespace JamUnity.Editor.Core
