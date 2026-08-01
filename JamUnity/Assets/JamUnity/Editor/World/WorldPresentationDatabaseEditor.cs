using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

using JamUnity.World.Presentation;
using JamUnity.World.Runtime;
using JamUnity.Authoring.World;

namespace JamUnity.Editor.World.Presentation
{
    [CustomEditor(typeof(WorldPresentationDatabase))]
    public sealed class WorldPresentationDatabaseEditor : UnityEditor.Editor
    {
        private const string DefaultWorldArchetypeDatabasePath = WorldArchetypeDatabase.DatabaseAssetPath;
        private SerializedProperty entriesProp;

        private void OnEnable()
        {
            entriesProp = serializedObject.FindProperty("entries");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.HelpBox(
                "WorldPresentationDatabase is derived from the Cache world archetype database and WorldAuthoring prefab bindings.",
                MessageType.Info);

            DrawActionButtons();
            DrawEntries();
            DrawWarnings((WorldPresentationDatabase)target);

            serializedObject.ApplyModifiedProperties();
        }

        private void DrawActionButtons()
        {
            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Sync From Cache + World Authoring"))
            {
                if (!TrySyncFromCacheAndWorldAuthoring((WorldPresentationDatabase)target, out string message))
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
            EditorGUILayout.LabelField("World Presentation Bindings", EditorStyles.boldLabel);

            if (entriesProp == null)
                return;

            for (int i = 0; i < entriesProp.arraySize; ++i)
            {
                SerializedProperty entryProp = entriesProp.GetArrayElementAtIndex(i);
                SerializedProperty worldArchetypeNameProp = entryProp.FindPropertyRelative("worldArchetypeName");
                SerializedProperty worldArchetypeKeyProp = entryProp.FindPropertyRelative("worldArchetypeKey");
                SerializedProperty worldPrefabProp = entryProp.FindPropertyRelative("worldPrefab");

                EditorGUILayout.BeginVertical(EditorStyles.helpBox);
                EditorGUILayout.LabelField($"Entry {i}", EditorStyles.boldLabel);
                using (new EditorGUI.DisabledScope(true))
                {
                    EditorGUILayout.PropertyField(worldArchetypeNameProp, new GUIContent("World Archetype Name"));
                    EditorGUILayout.PropertyField(worldArchetypeKeyProp, new GUIContent("World Archetype Key"));
                    EditorGUILayout.PropertyField(worldPrefabProp, new GUIContent("World Prefab"));
                }

                EditorGUILayout.EndVertical();
            }

            EditorGUILayout.Space();
        }

        private static void DrawWarnings(WorldPresentationDatabase database)
        {
            IReadOnlyList<WorldPresentationDatabase.Entry> entries = database.Entries;
            if (entries == null || entries.Count == 0)
            {
                EditorGUILayout.HelpBox("No world presentation bindings are registered. Use Sync From Cache + World Authoring.", MessageType.Warning);
                return;
            }

            WorldArchetypeDatabase archetypeDatabase = AssetDatabase.LoadAssetAtPath<WorldArchetypeDatabase>(DefaultWorldArchetypeDatabasePath);
            HashSet<string> seenNames = new(System.StringComparer.OrdinalIgnoreCase);
            HashSet<ulong> seenKeys = new();
            for (int i = 0; i < entries.Count; ++i)
            {
                WorldPresentationDatabase.Entry entry = entries[i];
                if (entry == null)
                    continue;

                if (string.IsNullOrWhiteSpace(entry.WorldArchetypeName))
                {
                    EditorGUILayout.HelpBox($"World presentation entry #{i} is missing world_archetype name.", MessageType.Warning);
                    continue;
                }

                if (!seenNames.Add(entry.WorldArchetypeName))
                    EditorGUILayout.HelpBox($"World presentation binding '{entry.WorldArchetypeName}' is duplicated.", MessageType.Warning);

                if (entry.WorldArchetypeKey == 0 || !seenKeys.Add(entry.WorldArchetypeKey))
                    EditorGUILayout.HelpBox($"World presentation binding '{entry.WorldArchetypeName}' has an invalid or duplicated archetype key.", MessageType.Warning);

                if (entry.WorldPrefab == null)
                {
                    EditorGUILayout.HelpBox($"World presentation binding '{entry.WorldArchetypeName}' is missing a world prefab.", MessageType.Warning);
                    continue;
                }

                if (!EditorUtility.IsPersistent(entry.WorldPrefab))
                    EditorGUILayout.HelpBox($"World presentation binding '{entry.WorldArchetypeName}' must reference a prefab asset, not a scene instance.", MessageType.Warning);

                WorldRoot worldRoot = entry.WorldPrefab.GetComponent<WorldRoot>();
                if (worldRoot == null)
                {
                    EditorGUILayout.HelpBox($"World prefab '{entry.WorldPrefab.name}' is missing WorldRoot.", MessageType.Warning);
                }

                WorldAuthoring worldAuthoring = entry.WorldPrefab.GetComponent<WorldAuthoring>();
                if (worldAuthoring == null)
                {
                    EditorGUILayout.HelpBox($"World prefab '{entry.WorldPrefab.name}' is missing WorldAuthoring.", MessageType.Warning);
                }
                else if (worldAuthoring.WorldArchetype == null)
                {
                    EditorGUILayout.HelpBox($"WorldAuthoring on '{entry.WorldPrefab.name}' is missing WorldArchetypeData.", MessageType.Warning);
                }
                else if (!string.Equals(worldAuthoring.WorldArchetype.AssetName, entry.WorldArchetypeName, System.StringComparison.OrdinalIgnoreCase))
                {
                    EditorGUILayout.HelpBox($"WorldAuthoring on '{entry.WorldPrefab.name}' does not match binding '{entry.WorldArchetypeName}'.", MessageType.Warning);
                }

                if (archetypeDatabase != null)
                {
                    bool referenced = false;
                    IReadOnlyList<WorldArchetypeData> archetypes = archetypeDatabase.Entries;
                    for (int j = 0; j < archetypes.Count; ++j)
                    {
                        WorldArchetypeData archetype = archetypes[j];
                        if (archetype == null)
                            continue;

                        if (string.Equals(archetype.AssetName, entry.WorldArchetypeName, System.StringComparison.OrdinalIgnoreCase))
                        {
                            referenced = true;
                            if (worldAuthoring != null && worldAuthoring.WorldArchetype != archetype)
                                EditorGUILayout.HelpBox($"WorldAuthoring on '{entry.WorldPrefab.name}' references a WorldArchetypeData outside the Cache database.", MessageType.Warning);
                            break;
                        }
                    }

                    if (!referenced)
                        EditorGUILayout.HelpBox($"World presentation binding '{entry.WorldArchetypeName}' is not referenced by any world archetype.", MessageType.Info);
                }
            }

            if (archetypeDatabase != null)
            {
                IReadOnlyList<WorldArchetypeData> archetypes = archetypeDatabase.Entries;
                for (int i = 0; i < archetypes.Count; ++i)
                {
                    WorldArchetypeData archetype = archetypes[i];
                    if (archetype == null || string.IsNullOrWhiteSpace(archetype.AssetName))
                        continue;

                    if (!database.TryGetEntry(archetype.AssetName, out _))
                        EditorGUILayout.HelpBox($"World archetype '{archetype.AssetName}' has no world presentation binding.", MessageType.Warning);
                }
            }
        }

        public static bool TrySyncFromCacheAndWorldAuthoring(WorldPresentationDatabase database, out string message)
        {
            message = string.Empty;
            if (database == null)
            {
                message = "WorldPresentationDatabase is null.";
                return false;
            }

            WorldArchetypeDatabase cacheDatabase = AssetDatabase.LoadAssetAtPath<WorldArchetypeDatabase>(DefaultWorldArchetypeDatabasePath);
            if (cacheDatabase == null)
            {
                message = $"WorldArchetypeDatabase was not found at '{DefaultWorldArchetypeDatabasePath}'.";
                return false;
            }

            Dictionary<WorldArchetypeData, GameObject> prefabsByArchetype = BuildWorldAuthoringIndex();
            Undo.RecordObject(database, "Sync World Presentation Database From Cache");

            List<WorldPresentationDatabase.Entry> nextEntries = new();
            HashSet<string> seenNames = new(System.StringComparer.OrdinalIgnoreCase);
            IReadOnlyList<WorldArchetypeData> archetypes = cacheDatabase.Entries;
            for (int i = 0; i < archetypes.Count; ++i)
            {
                WorldArchetypeData archetype = archetypes[i];
                if (archetype == null)
                    continue;

                string archetypeName = archetype.AssetName?.Trim() ?? string.Empty;
                if (string.IsNullOrWhiteSpace(archetypeName))
                {
                    Debug.LogError($"WorldArchetypeDatabase contains an archetype with no name at index {i}.");
                    continue;
                }

                if (!seenNames.Add(archetypeName))
                {
                    Debug.LogError($"WorldArchetypeDatabase contains duplicate world archetype name '{archetypeName}'.");
                    continue;
                }

                prefabsByArchetype.TryGetValue(archetype, out GameObject prefab);
                nextEntries.Add(new WorldPresentationDatabase.Entry
                {
                    WorldArchetypeName = archetypeName,
                    WorldPrefab = prefab,
                });
            }

            database.SetEntries(nextEntries);
            EditorUtility.SetDirty(database);
            AssetDatabase.SaveAssets();
            message = $"Synced {nextEntries.Count} world presentation bindings from '{DefaultWorldArchetypeDatabasePath}'.";
            return true;
        }

        private static Dictionary<WorldArchetypeData, GameObject> BuildWorldAuthoringIndex()
        {
            Dictionary<WorldArchetypeData, GameObject> result = new();
            string[] prefabGuids = AssetDatabase.FindAssets("t:Prefab", new[] { "Assets" });
            for (int i = 0; i < prefabGuids.Length; ++i)
            {
                string path = AssetDatabase.GUIDToAssetPath(prefabGuids[i]);
                GameObject prefab = AssetDatabase.LoadAssetAtPath<GameObject>(path);
                WorldAuthoring authoring = prefab != null ? prefab.GetComponent<WorldAuthoring>() : null;
                if (authoring == null || authoring.WorldArchetype == null)
                    continue;

                if (!result.TryAdd(authoring.WorldArchetype, prefab))
                {
                    Debug.LogError($"Multiple WorldAuthoring prefabs reference world archetype '{authoring.WorldArchetype.AssetName}'.");
                    result[authoring.WorldArchetype] = null;
                }
            }

            return result;
        }
    }
} // namespace JamUnity.Editor.World.Presentation
