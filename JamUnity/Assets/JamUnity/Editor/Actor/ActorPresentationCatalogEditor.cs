using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

using JamUnity.Actor.Runtime;
using JamUnity.Authoring.Actor;
using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Actor
{
    [CustomEditor(typeof(ActorPresentationCatalog))]
    public sealed class ActorPresentationCatalogEditor : UnityEditor.Editor
    {
        private SerializedProperty entriesProp;

        private void OnEnable()
        {
            entriesProp = serializedObject.FindProperty("entries");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();
            EditorGUILayout.HelpBox(
                "ActorPresentationCatalog is the global Unity-only mapping from actor archetype identity to prefab.",
                MessageType.Info);

            DrawActionButtons();
            DrawEntries();
            DrawWarnings((ActorPresentationCatalog)target);
            serializedObject.ApplyModifiedProperties();
        }

        private void DrawActionButtons()
        {
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("Sort/Clean"))
            {
                serializedObject.ApplyModifiedProperties();
                ActorPresentationCatalog catalog = (ActorPresentationCatalog)target;
                Undo.RecordObject(catalog, "Sort Actor Presentation Catalog");
                catalog.SortAndClean();
                EditorUtility.SetDirty(catalog);
                serializedObject.Update();
            }

            if (GUILayout.Button("Sync Actor Archetypes"))
            {
                if (!TrySyncFromActorArchetypes((ActorPresentationCatalog)target, out string message))
                    Debug.LogError(message);
                serializedObject.Update();
            }

            if (GUILayout.Button("Reload Inspector"))
                Repaint();
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.Space();
        }

        private void DrawEntries()
        {
            EditorGUILayout.LabelField("Global Actor Presentation Bindings", EditorStyles.boldLabel);
            if (entriesProp == null)
                return;

            for (int i = 0; i < entriesProp.arraySize; ++i)
            {
                SerializedProperty entryProp = entriesProp.GetArrayElementAtIndex(i);
                SerializedProperty nameProp = entryProp.FindPropertyRelative("actorArchetypeName");
                SerializedProperty keyProp = entryProp.FindPropertyRelative("actorArchetypeKey");
                SerializedProperty prefabProp = entryProp.FindPropertyRelative("actorPrefab");

                EditorGUILayout.BeginVertical(EditorStyles.helpBox);
                EditorGUILayout.LabelField($"Entry {i}", EditorStyles.boldLabel);
                using (new EditorGUI.DisabledScope(true))
                {
                    EditorGUILayout.PropertyField(nameProp, new GUIContent("Actor Archetype Name"));
                    EditorGUILayout.PropertyField(keyProp, new GUIContent("Actor Archetype Key"));
                }
                EditorGUILayout.PropertyField(prefabProp, new GUIContent("Actor Prefab"));

                ActorPresentationCatalog.Entry entry = ((ActorPresentationCatalog)target).Entries[i];
                if (entry != null)
                {
                    EditorGUILayout.BeginHorizontal();
                    if (GUILayout.Button("Create Prefab"))
                        ActorCreationEditor.CreateActorPrefabAndLink((ActorPresentationCatalog)target, entry.ActorArchetypeName);
                    using (new EditorGUI.DisabledScope(Selection.activeGameObject == null))
                    {
                        if (GUILayout.Button("Save Selected ActorRoot"))
                            ActorCreationEditor.SaveActorRootAsPrefabAndLink((ActorPresentationCatalog)target, entry.ActorArchetypeName, Selection.activeGameObject);
                    }
                    EditorGUILayout.EndHorizontal();
                }
                EditorGUILayout.EndVertical();
            }
            EditorGUILayout.Space();
        }

        private static void DrawWarnings(ActorPresentationCatalog catalog)
        {
            IReadOnlyList<ActorPresentationCatalog.Entry> entries = catalog.Entries;
            if (entries == null || entries.Count == 0)
            {
                EditorGUILayout.HelpBox("No actor presentation bindings are registered.", MessageType.Warning);
                return;
            }

            Dictionary<string, ActorArchetypeData> archetypes = BuildActorArchetypeIndex(out HashSet<string> ambiguousNames);
            HashSet<string> seenNames = new(System.StringComparer.OrdinalIgnoreCase);
            HashSet<ulong> seenKeys = new();
            foreach (ActorPresentationCatalog.Entry entry in entries)
            {
                if (entry == null)
                    continue;

                if (string.IsNullOrWhiteSpace(entry.ActorArchetypeName))
                {
                    EditorGUILayout.HelpBox("Actor presentation entry is missing actor_archetype name.", MessageType.Warning);
                    continue;
                }
                if (!seenNames.Add(entry.ActorArchetypeName))
                    EditorGUILayout.HelpBox($"Actor presentation binding '{entry.ActorArchetypeName}' is duplicated.", MessageType.Warning);
                if (entry.ActorArchetypeKey == 0 || !seenKeys.Add(entry.ActorArchetypeKey))
                    EditorGUILayout.HelpBox($"Actor presentation binding '{entry.ActorArchetypeName}' has an invalid or duplicated key.", MessageType.Warning);
                if (ambiguousNames.Contains(entry.ActorArchetypeName))
                    EditorGUILayout.HelpBox($"Actor archetype '{entry.ActorArchetypeName}' has multiple generated definitions. Consolidate them before synchronizing its prefab physics reference.", MessageType.Warning);
                if (entry.ActorPrefab == null)
                {
                    EditorGUILayout.HelpBox($"Actor presentation binding '{entry.ActorArchetypeName}' has no prefab.", MessageType.Warning);
                    continue;
                }
                if (!EditorUtility.IsPersistent(entry.ActorPrefab))
                    EditorGUILayout.HelpBox($"Actor presentation binding '{entry.ActorArchetypeName}' must reference a prefab asset.", MessageType.Warning);
                if (!ambiguousNames.Contains(entry.ActorArchetypeName)
                    && archetypes.TryGetValue(entry.ActorArchetypeName, out ActorArchetypeData archetype))
                    DrawPrefabWarnings(entry.ActorArchetypeName, entry.ActorPrefab, archetype.PhysicsArchetype);
            }

            foreach (KeyValuePair<string, ActorArchetypeData> pair in archetypes)
            {
                if (!catalog.TryGetEntry(pair.Key, out _))
                    EditorGUILayout.HelpBox($"Actor archetype '{pair.Key}' has no presentation binding.", MessageType.Warning);
            }
        }

        public static bool TrySyncFromActorArchetypes(ActorPresentationCatalog catalog, out string message)
        {
            if (catalog == null)
            {
                message = "ActorPresentationCatalog is null.";
                return false;
            }

            Dictionary<string, ActorArchetypeData> archetypes = BuildActorArchetypeIndex(out _);
            Dictionary<string, ActorPresentationCatalog.Entry> existingByName = new(System.StringComparer.OrdinalIgnoreCase);
            foreach (ActorPresentationCatalog.Entry entry in catalog.Entries)
            {
                if (entry != null && !string.IsNullOrWhiteSpace(entry.ActorArchetypeName))
                    existingByName[entry.ActorArchetypeName] = entry;
            }

            Undo.RecordObject(catalog, "Sync Actor Presentation Catalog");
            List<ActorPresentationCatalog.Entry> nextEntries = new();
            foreach (string archetypeName in archetypes.Keys)
            {
                if (existingByName.TryGetValue(archetypeName, out ActorPresentationCatalog.Entry existing))
                    nextEntries.Add(existing);
                else
                    nextEntries.Add(new ActorPresentationCatalog.Entry { ActorArchetypeName = archetypeName });
            }

            catalog.SetEntries(nextEntries);
            EditorUtility.SetDirty(catalog);
            AssetDatabase.SaveAssets();
            message = $"Synced {nextEntries.Count} global actor presentation bindings.";
            return true;
        }

        public static ActorPresentationCatalog FindCatalog()
        {
            string[] guids = AssetDatabase.FindAssets("t:ActorPresentationCatalog", new[] { "Assets" });
            if (guids.Length != 1)
                return null;
            return AssetDatabase.LoadAssetAtPath<ActorPresentationCatalog>(AssetDatabase.GUIDToAssetPath(guids[0]));
        }

        private static Dictionary<string, ActorArchetypeData> BuildActorArchetypeIndex(out HashSet<string> ambiguousNames)
        {
            Dictionary<string, ActorArchetypeData> result = new(System.StringComparer.OrdinalIgnoreCase);
            ambiguousNames = new HashSet<string>(System.StringComparer.OrdinalIgnoreCase);
            ActorArchetypeDatabase database = AssetDatabase.LoadAssetAtPath<ActorArchetypeDatabase>(ActorArchetypeDatabase.DatabaseAssetPath);
            if (database != null)
            {
                foreach (ActorArchetypeData archetype in database.Archetypes)
                {
                    string name = archetype != null ? archetype.ArchetypeName : string.Empty;
                    if (string.IsNullOrWhiteSpace(name))
                        continue;
                    if (!result.TryAdd(name, archetype) && result[name] != archetype)
                        ambiguousNames.Add(name);
                }
            }
            return result;
        }

        private static void DrawPrefabWarnings(string archetypeName, GameObject actorPrefab, PhysicsArchetypeData physicsArchetype)
        {
            if (actorPrefab.GetComponent<ActorRootMarker>() == null)
                EditorGUILayout.HelpBox($"Actor presentation binding '{archetypeName}' prefab is missing ActorRootMarker.", MessageType.Warning);

            ActorPhysicalMarker marker = actorPrefab.GetComponentInChildren<ActorPhysicalMarker>(true);
            if (physicsArchetype == null)
                return;
            if (marker == null)
            {
                EditorGUILayout.HelpBox($"Actor presentation binding '{archetypeName}' requires a Physical part.", MessageType.Warning);
                return;
            }
            PhysicsArchetypeAuthoring authoring = marker.GetComponent<PhysicsArchetypeAuthoring>();
            if (authoring == null || authoring.PhysicsArchetype != physicsArchetype)
                EditorGUILayout.HelpBox($"Actor presentation binding '{archetypeName}' physics archetype does not match the indexed actor archetype.", MessageType.Warning);
        }
    }
}
