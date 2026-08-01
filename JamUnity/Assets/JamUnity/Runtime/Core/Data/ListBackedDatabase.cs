using System.Collections.Generic;
using UnityEngine;

namespace JamUnity.Core.Data
{
    public abstract class ListBackedDatabase<TEntry> : ScriptableObject, ISerializationCallbackReceiver
        where TEntry : class
    {
        protected abstract List<TEntry> EntryStorage { get; set; }
    
        protected IReadOnlyList<TEntry> EntriesReadonly => EntryStorage;
    
        private bool lookupBuilt;
    
        protected void SetEntriesInternal(IEnumerable<TEntry> value)
        {
            EntryStorage = value != null ? new List<TEntry>(value) : new List<TEntry>();
            SortAndCleanInternal();
        }
    
        protected void SortAndCleanInternal()
        {
            List<TEntry> entries = EntryStorage;
            if (entries == null)
            {
                EntryStorage = new List<TEntry>();
                RebuildLookupNow();
                return;
            }
    
            entries.RemoveAll(IsNullEntry);
            for (int i = 0; i < entries.Count; ++i)
                NormalizeEntry(entries[i]);
    
            entries.Sort(CompareEntries);
            RebuildLookupNow();
        }
    
        protected void EnsureLookupBuilt()
        {
            if (!lookupBuilt)
                RebuildLookupNow();
        }
    
        protected void RebuildLookupNow()
        {
            RebuildLookupCore();
            lookupBuilt = true;
        }
    
        protected virtual bool IsNullEntry(TEntry entry)
        {
            return entry == null;
        }
    
        protected virtual void NormalizeEntry(TEntry entry)
        {
        }
    
        protected abstract int CompareEntries(TEntry lhs, TEntry rhs);
        protected abstract void RebuildLookupCore();
    
        public void OnBeforeSerialize()
        {
        }
    
        public void OnAfterDeserialize()
        {
            RebuildLookupNow();
        }
    
        protected virtual void OnValidate()
        {
            SortAndCleanInternal();
        }
    }

} // namespace JamUnity.Core.Data

