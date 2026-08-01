using System;
using System.Collections.Generic;
using UnityEngine;

using JamUnity.Core.Data;

namespace JamUnity.World.Presentation
{
    [CreateAssetMenu(menuName = "JamUnity/World Presentation Database", fileName = "WorldPresentationDatabase")]
    public sealed class WorldPresentationDatabase : ListBackedDatabase<WorldPresentationDatabase.Entry>
    {
        [Serializable]
        public sealed class Entry
        {
            [SerializeField] private string worldArchetypeName;
            [SerializeField, HideInInspector] private ulong worldArchetypeKey;
            [SerializeField] private GameObject worldPrefab;

            public string WorldArchetypeName
            {
                get => worldArchetypeName?.Trim() ?? string.Empty;
                set => worldArchetypeName = value?.Trim() ?? string.Empty;
            }

            public ulong WorldArchetypeKey => worldArchetypeKey;

            public GameObject WorldPrefab
            {
                get => worldPrefab;
                set => worldPrefab = value;
            }

            public void SyncDerivedKey()
            {
                worldArchetypeKey = Core.Util.StableKey.MakeStableKey(WorldArchetypeName);
            }
        }

        [SerializeField] private List<Entry> entries = new();

        [NonSerialized] private readonly Dictionary<string, Entry> entriesByName = new(StringComparer.OrdinalIgnoreCase);
        [NonSerialized] private readonly Dictionary<ulong, Entry> entriesByKey = new();

        public IReadOnlyList<Entry> Entries => EntriesReadonly;

        public void SetEntries(IEnumerable<Entry> value)
        {
            SetEntriesInternal(value);
        }

        public void SortAndClean()
        {
            SortAndCleanInternal();
        }

        public bool TryGetEntry(ulong worldArchetypeKey, out Entry entry)
        {
            EnsureLookupBuilt();
            entry = null;
            return worldArchetypeKey != 0
                && entriesByKey.TryGetValue(worldArchetypeKey, out entry)
                && entry != null;
        }

        public bool TryGetEntry(string worldArchetypeName, out Entry entry)
        {
            EnsureLookupBuilt();
            entry = null;
            string archetypeName = worldArchetypeName?.Trim() ?? string.Empty;
            return !string.IsNullOrWhiteSpace(archetypeName)
                && entriesByName.TryGetValue(archetypeName, out entry)
                && entry != null;
        }

        public bool TryGetWorldPrefab(ulong worldArchetypeKey, out GameObject worldPrefab)
        {
            worldPrefab = null;
            return TryGetEntry(worldArchetypeKey, out Entry entry)
                && (worldPrefab = entry.WorldPrefab) != null;
        }

        protected override List<Entry> EntryStorage
        {
            get => entries;
            set => entries = value;
        }

        protected override void NormalizeEntry(Entry entry)
        {
            entry.WorldArchetypeName = entry.WorldArchetypeName;
            entry.SyncDerivedKey();
        }

        protected override int CompareEntries(Entry lhs, Entry rhs)
        {
            return string.CompareOrdinal(lhs.WorldArchetypeName, rhs.WorldArchetypeName);
        }

        protected override void RebuildLookupCore()
        {
            entriesByName.Clear();
            entriesByKey.Clear();
            if (entries == null)
                return;

            foreach (var entry in entries)
            {
                if (entry == null)
                    continue;

                if (!string.IsNullOrWhiteSpace(entry.WorldArchetypeName))
                    entriesByName.TryAdd(entry.WorldArchetypeName, entry);

                if (entry.WorldArchetypeKey != 0)
                    entriesByKey.TryAdd(entry.WorldArchetypeKey, entry);
            }
        }
    }

} // namespace JamUnity.World.Presentation
