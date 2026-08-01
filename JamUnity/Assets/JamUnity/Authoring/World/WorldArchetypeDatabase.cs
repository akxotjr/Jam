using System;
using System.Collections.Generic;
using UnityEngine;

using JamUnity.Core.Data;

namespace JamUnity.Authoring.World
{
    [CreateAssetMenu(menuName = "JamUnity/World Archetype Database", fileName = "WorldArchetypeDatabase")]
    public sealed class WorldArchetypeDatabase : ListBackedDatabase<WorldArchetypeData>
    {
        public const string DatabaseAssetPath = Core.Util.Path.GeneratedAssetRoot + "/World/WorldArchetypeDatabase.asset";
        public const string WorldArchetypeDataRoot = Core.Util.Path.GeneratedAssetRoot + "/World/Archetypes";
        public const string DefaultSharedDataPath = JamUnity.Core.Util.Path.WorldArchetypesPath;
    
        [SerializeField] private string worldArchetypesAssetPath = DefaultSharedDataPath;
        [SerializeField] private int    version = 1;
        [SerializeField] private List<WorldArchetypeData> entries = new();
    
        [NonSerialized] private readonly Dictionary<string, WorldArchetypeData> entriesByName = new(StringComparer.Ordinal);
    
        public string WorldArchetypesAssetPath => worldArchetypesAssetPath?.Trim() ?? string.Empty;
        public string ResolvedWorldArchetypesAssetPath => JamUnity.Core.Util.Path.ResolveSharedDataPath(WorldArchetypesAssetPath);
        public int Version => version > 0 ? version : 1;
        public IReadOnlyList<WorldArchetypeData> Entries => EntriesReadonly;
        public IReadOnlyDictionary<string, WorldArchetypeData> EntriesByName
        {
            get
            {
                EnsureLookupBuilt();
                return entriesByName;
            }
        }
    
        public void SetWorldArchetypesAssetPath(string value)
        {
            worldArchetypesAssetPath = value?.Trim() ?? string.Empty;
        }
    
        public void SetVersion(int value)
        {
            version = value > 0 ? value : 1;
        }
    
        public void SetEntries(IEnumerable<WorldArchetypeData> value)
        {
            SetEntriesInternal(value);
        }
    
        public void SortAndClean()
        {
            SortAndCleanInternal();
        }
    
        public bool TryGetArchetype(string archetypeName, out WorldArchetypeData archetype)
        {
            EnsureLookupBuilt();
            archetype = null;
            string normalizedArchetypeName = archetypeName?.Trim() ?? string.Empty;
            return !string.IsNullOrWhiteSpace(normalizedArchetypeName)
                && entriesByName.TryGetValue(normalizedArchetypeName, out archetype)
                && archetype != null;
        }
    
        protected override List<WorldArchetypeData> EntryStorage
        {
            get => entries;
            set => entries = value;
        }
    
        protected override int CompareEntries(WorldArchetypeData lhs, WorldArchetypeData rhs)
        {
            return string.CompareOrdinal(lhs.AssetName, rhs.AssetName);
        }
    
        protected override void RebuildLookupCore()
        {
            entriesByName.Clear();
    
            for (int i = 0; i < entries.Count; ++i)
            {
                WorldArchetypeData entry = entries[i];
                if (entry == null)
                    continue;
    
                string archetypeName = entry.AssetName?.Trim() ?? string.Empty;
                if (!string.IsNullOrWhiteSpace(archetypeName))
                    entriesByName.TryAdd(archetypeName, entry);
            }
        }
    }
    
} // namespace JamUnity.Authoring.World
