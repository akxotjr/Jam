using System;
using System.Collections.Generic;
using UnityEngine;

using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Actor
{
    [CreateAssetMenu(menuName = "JamUnity/Actor Archetype Database", fileName = "ActorArchetypeDatabase")]
    public sealed class ActorArchetypeDatabase : ListBackedDatabase<ActorArchetypeData>
    {
        public const string DatabaseAssetPath = Core.Util.Path.GeneratedAssetRoot + "/Actor/ActorArchetypeDatabase.asset";
        public const string ActorArchetypeDataRoot = Core.Util.Path.GeneratedAssetRoot + "/Actor/Archetypes";
        public const string DefaultSharedDataPath = JamUnity.Core.Util.Path.ActorArchetypesPath;

        [SerializeField] private string                     sharedDataPath      = DefaultSharedDataPath;
        [SerializeField] private int                        sharedDataVersion   = 1;
        [SerializeField] private List<ActorArchetypeData>   archetypes          = new();

        [NonSerialized] private readonly Dictionary<string, ActorArchetypeData> archetypesByName = new(StringComparer.Ordinal);
        
        public string                   SharedDataPath      => sharedDataPath?.Trim() ?? string.Empty;
        public int                      SharedDataVersion   => sharedDataVersion;
        public List<ActorArchetypeData> Archetypes          => archetypes;

        public void SetSharedDataPath(string value)
        {
            sharedDataPath = value?.Trim() ?? string.Empty;
        }
        public IReadOnlyDictionary<string, ActorArchetypeData> ArchetypesByName
        {
            get
            {
                EnsureLookupBuilt();
                return archetypesByName;
            }
        }

        public void ApplySharedSync(int version, List<ActorArchetypeData> syncedArchetypes)
        {
            sharedDataVersion = version > 0 ? version : 1;
            archetypes.Clear();
            if (syncedArchetypes != null)
                archetypes.AddRange(syncedArchetypes);
            SortAndCleanInternal();
        }

        public bool TryValidateEntries(out List<string> errors)
        {
            EnsureLookupBuilt();
            errors = new List<string>();

            var seenNames = new HashSet<string>(StringComparer.Ordinal);

            for (int i = 0; i < archetypes.Count; ++i)
            {
                ActorArchetypeData archetype = archetypes[i];
                if (archetype == null)
                {
                    errors.Add($"Entry #{i} is null.");
                    continue;
                }

                string archetypeName = archetype.AssetName?.Trim() ?? string.Empty;
                string entryLabel = string.IsNullOrWhiteSpace(archetypeName) ? $"row#{i}" : archetypeName;
                if (string.IsNullOrWhiteSpace(archetypeName))
                {
                    errors.Add($"[{entryLabel}] AssetName is empty.");
                    continue;
                }

                if (!seenNames.Add(archetypeName))
                    errors.Add($"Duplicate archetype AssetName '{archetypeName}'.");
            }

            return errors.Count == 0;
        }

        public bool TryGetArchetype(string archetypeName, out ActorArchetypeData archetype)
        {
            EnsureLookupBuilt();
            archetype = null;

            string normalizedArchetypeName = archetypeName?.Trim() ?? string.Empty;
            if (string.IsNullOrWhiteSpace(normalizedArchetypeName))
                return false;

            return archetypesByName.TryGetValue(normalizedArchetypeName, out archetype) && archetype != null;
        }

        public bool TryBuildSharedRows(out Dictionary<string, SharedGen.ActorArchetypeDto> rows, out List<string> errors)
        {
            rows = new Dictionary<string, SharedGen.ActorArchetypeDto>(StringComparer.Ordinal);
            if (!TryValidateEntries(out errors))
                return false;

            for (int i = 0; i < archetypes.Count; ++i)
            {
                ActorArchetypeData archetype = archetypes[i];
                if (archetype == null)
                    continue;

                string archetypeName = archetype.AssetName?.Trim() ?? string.Empty;
                SharedGen.ActorArchetypeDto dto = archetype.ToDto();
                if (dto == null)
                {
                    errors.Add($"[{archetypeName}] failed to build dto from ActorArchetypeData.");
                    continue;
                }

                rows[archetypeName] = dto;
            }

            if (errors.Count != 0)
                return false;

            rows = BuildSortedSharedMap(rows);
            return true;
        }

        protected override List<ActorArchetypeData> EntryStorage
        {
            get => archetypes;
            set => archetypes = value;
        }

        protected override int CompareEntries(ActorArchetypeData lhs, ActorArchetypeData rhs)
        {
            return string.CompareOrdinal(lhs.AssetName, rhs.AssetName);
        }

        protected override void RebuildLookupCore()
        {
            archetypesByName.Clear();

            for (int i = 0; i < archetypes.Count; ++i)
            {
                ActorArchetypeData archetype = archetypes[i];
                if (archetype == null)
                    continue;

                string archetypeName = archetype.AssetName?.Trim() ?? string.Empty;
                if (!string.IsNullOrWhiteSpace(archetypeName) && !archetypesByName.ContainsKey(archetypeName))
                    archetypesByName.Add(archetypeName, archetype);
            }
        }

        private static Dictionary<string, SharedGen.ActorArchetypeDto> BuildSortedSharedMap(Dictionary<string, SharedGen.ActorArchetypeDto> source)
        {
            var result = new Dictionary<string, SharedGen.ActorArchetypeDto>(source.Count, StringComparer.Ordinal);
            var sortedArchetypeNames = new List<string>(source.Keys);
            sortedArchetypeNames.Sort(StringComparer.Ordinal);
            for (int i = 0; i < sortedArchetypeNames.Count; ++i)
                result.Add(sortedArchetypeNames[i], source[sortedArchetypeNames[i]]);
            return result;
        }
    }
    
} // namespace JamUnity.Editor.Actor.Archetype
