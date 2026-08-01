using System;
using System.Collections.Generic;
using UnityEngine;

using JamUnity.Core.Data;

namespace JamUnity.Authoring.World
{
    [CreateAssetMenu(menuName = "JamUnity/World Template Database", fileName = "WorldTemplateDatabase")]
    public sealed class WorldTemplateDatabase : ListBackedDatabase<WorldTemplateData>
    {
        public const string DatabaseAssetPath = Core.Util.Path.GeneratedAssetRoot + "/World/WorldTemplateDatabase.asset";
        public const string WorldTemplateDataRoot = Core.Util.Path.GeneratedAssetRoot + "/World/Templates";
        public const string DefaultSharedDataPath = JamUnity.Core.Util.Path.WorldTemplatesPath;
    
        [SerializeField] private string worldTemplateAssetPath = DefaultSharedDataPath;
        [SerializeField] private int version = 1;
        [SerializeField] private List<WorldTemplateData> entries = new();
    
        [NonSerialized] private readonly Dictionary<string, WorldTemplateData> entriesByName = new(StringComparer.Ordinal);
    
        public string WorldTemplateAssetPath => worldTemplateAssetPath?.Trim() ?? string.Empty;
        public string ResolvedWorldTemplateAssetPath => JamUnity.Core.Util.Path.ResolveSharedDataPath(WorldTemplateAssetPath);
        public int Version => version > 0 ? version : 1;
        public IReadOnlyList<WorldTemplateData> Entries => EntriesReadonly;
        public IReadOnlyDictionary<string, WorldTemplateData> EntriesByName
        {
            get
            {
                EnsureLookupBuilt();
                return entriesByName;
            }
        }
    
        public void SetWorldTemplateAssetPath(string value)
        {
            worldTemplateAssetPath = value?.Trim() ?? string.Empty;
        }
    
        public void SetVersion(int value)
        {
            version = value > 0 ? value : 1;
        }
    
        public void SetEntries(IEnumerable<WorldTemplateData> value)
        {
            SetEntriesInternal(value);
        }
    
        public void SortAndClean()
        {
            SortAndCleanInternal();
        }
    
        public bool TryGetTemplate(string templateName, out WorldTemplateData template)
        {
            EnsureLookupBuilt();
            template = null;
            string normalizedTemplateName = templateName?.Trim() ?? string.Empty;
            return !string.IsNullOrWhiteSpace(normalizedTemplateName)
                && entriesByName.TryGetValue(normalizedTemplateName, out template)
                && template != null;
        }
    
        protected override List<WorldTemplateData> EntryStorage
        {
            get => entries;
            set => entries = value;
        }
    
        protected override int CompareEntries(WorldTemplateData lhs, WorldTemplateData rhs)
        {
            return string.CompareOrdinal(lhs.AssetName, rhs.AssetName);
        }
    
        protected override void RebuildLookupCore()
        {
            entriesByName.Clear();
    
            for (int i = 0; i < entries.Count; ++i)
            {
                WorldTemplateData entry = entries[i];
                if (entry == null)
                    continue;
    
                string templateAssetName = entry.AssetName?.Trim() ?? string.Empty;
                if (!string.IsNullOrWhiteSpace(templateAssetName) && !entriesByName.ContainsKey(templateAssetName))
                    entriesByName.Add(templateAssetName, entry);
            }
        }
    }
} // namespace JamUnity.Editor.World.Template
