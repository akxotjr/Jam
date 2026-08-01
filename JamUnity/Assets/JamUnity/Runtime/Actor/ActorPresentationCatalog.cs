using System;
using System.Collections.Generic;
using JamUnity.Core.Data;
using JamUnity.Authoring.Actor;
using UnityEngine;

namespace JamUnity.Actor.Runtime
{
    [CreateAssetMenu(menuName = "JamUnity/Actor Presentation Catalog", fileName = "ActorPresentationCatalog")]
    public sealed class ActorPresentationCatalog : ListBackedDatabase<ActorPresentationCatalog.Entry>
    {
        [Serializable]
        public sealed class Entry
        {
            [SerializeField] private string actorArchetypeName;
            [SerializeField, HideInInspector] private ulong actorArchetypeKey;
            [SerializeField] private GameObject actorPrefab;

            public string ActorArchetypeName
            {
                get => actorArchetypeName?.Trim() ?? string.Empty;
                set => actorArchetypeName = value?.Trim() ?? string.Empty;
            }

            public ulong ActorArchetypeKey => actorArchetypeKey;

            public GameObject ActorPrefab
            {
                get => actorPrefab;
                set => actorPrefab = value;
            }

            public void SyncDerivedKey()
            {
                actorArchetypeKey = Core.Util.StableKey.MakeStableKey(ActorArchetypeName);
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

        public bool TryGetEntry(ulong actorArchetypeKey, out Entry entry)
        {
            EnsureLookupBuilt();
            entry = null;
            return actorArchetypeKey != 0
                && entriesByKey.TryGetValue(actorArchetypeKey, out entry)
                && entry != null;
        }

        public bool TryGetEntry(string actorArchetypeName, out Entry entry)
        {
            EnsureLookupBuilt();
            entry = null;
            string archetypeName = actorArchetypeName?.Trim() ?? string.Empty;
            return !string.IsNullOrWhiteSpace(archetypeName)
                && entriesByName.TryGetValue(archetypeName, out entry)
                && entry != null;
        }

        public bool TryGetActorPrefab(ulong actorArchetypeKey, out GameObject actorPrefab)
        {
            actorPrefab = null;
            return TryGetEntry(actorArchetypeKey, out Entry entry)
                && (actorPrefab = entry.ActorPrefab) != null;
        }

        public bool TryGetActorPrefab(string actorArchetypeName, out GameObject actorPrefab)
        {
            actorPrefab = null;
            return TryGetEntry(actorArchetypeName, out Entry entry)
                && (actorPrefab = entry.ActorPrefab) != null;
        }

        protected override List<Entry> EntryStorage
        {
            get => entries;
            set => entries = value;
        }

        protected override void NormalizeEntry(Entry entry)
        {
            entry.ActorArchetypeName = entry.ActorArchetypeName;
            entry.SyncDerivedKey();
        }

        protected override int CompareEntries(Entry lhs, Entry rhs)
        {
            return string.CompareOrdinal(lhs.ActorArchetypeName, rhs.ActorArchetypeName);
        }

        protected override void RebuildLookupCore()
        {
            entriesByName.Clear();
            entriesByKey.Clear();
            if (entries == null)
                return;

            for (int i = 0; i < entries.Count; ++i)
            {
                Entry entry = entries[i];
                if (entry == null)
                    continue;

                string actorArchetypeName = entry.ActorArchetypeName;
                if (!string.IsNullOrWhiteSpace(actorArchetypeName) && !entriesByName.ContainsKey(actorArchetypeName))
                    entriesByName.Add(actorArchetypeName, entry);

                if (entry.ActorArchetypeKey != 0 && !entriesByKey.ContainsKey(entry.ActorArchetypeKey))
                    entriesByKey.Add(entry.ActorArchetypeKey, entry);
            }
        }
    }
} // namespace JamUnity.Actor.Runtime
