using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.World;


namespace JamUnity.Editor.World
{
    [CustomEditor(typeof(WorldTemplateDatabase))]
    public sealed class WorldTemplateDatabaseEditor : UnityEditor.Editor
    {
        private SerializedProperty worldTemplateAssetPathProp;
        private SerializedProperty versionProp;
        private SerializedProperty entriesProp;
        private bool showEntries = true;

        private void OnEnable()
        {
            worldTemplateAssetPathProp = serializedObject.FindProperty("worldTemplateAssetPath");
            versionProp = serializedObject.FindProperty("version");
            entriesProp = serializedObject.FindProperty("entries");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            WorldTemplateDatabase database = (WorldTemplateDatabase)target;

            EditorGUILayout.HelpBox(
                "WorldTemplateDatabase is the Unity-side cache/editor surface for world_templates.json. Runtime client-world presentation resolution stays on WorldManager.",
                MessageType.Info);

            DrawActionButtons();
            DrawConfiguration();
            DrawEntries();
            DrawReferenceWarnings(database);

            serializedObject.ApplyModifiedProperties();
        }

        private void DrawConfiguration()
        {
            EditorGUILayout.LabelField("Configuration", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(worldTemplateAssetPathProp, new GUIContent("Manifest-Relative File"));
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextField("Database Asset Path", WorldTemplateDatabase.DatabaseAssetPath);
                EditorGUILayout.TextField("Asset Data Root", WorldTemplateDatabase.WorldTemplateDataRoot);
                EditorGUILayout.PropertyField(versionProp, new GUIContent("Version"));
            }
            EditorGUILayout.Space();
        }

        private void DrawEntries()
        {
            if (entriesProp == null)
                return;

            showEntries = EditorGUILayout.Foldout(showEntries, "World Templates", true);
            if (!showEntries)
                return;

            for (int i = 0; i < entriesProp.arraySize; ++i)
                DrawEntry(entriesProp.GetArrayElementAtIndex(i), i);

            using (new EditorGUILayout.HorizontalScope())
            {
                GUILayout.FlexibleSpace();
                if (GUILayout.Button("Add Entry", GUILayout.Width(120)))
                    entriesProp.InsertArrayElementAtIndex(entriesProp.arraySize);
            }

            EditorGUILayout.Space();
        }

        private void DrawEntry(SerializedProperty templateProp, int index)
        {
            WorldTemplateData template = templateProp.objectReferenceValue as WorldTemplateData;

            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField($"Entry {index}", EditorStyles.boldLabel);
            if (GUILayout.Button("Remove", GUILayout.Width(70)))
            {
                entriesProp.DeleteArrayElementAtIndex(index);
                EditorGUILayout.EndHorizontal();
                EditorGUILayout.EndVertical();
                return;
            }
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.PropertyField(templateProp, new GUIContent("Template Data"));
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextField("Asset Name", template != null ? template.AssetName : string.Empty);
                EditorGUILayout.TextField("Capacity", template != null ? template.Capacity.ToString() : string.Empty);
            }

            EditorGUILayout.EndVertical();
        }

        private void DrawActionButtons()
        {
            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Import World Templates"))
            {
                serializedObject.ApplyModifiedProperties();
                if (WorldTemplateAssetImporter.TryImport(out string message))
                    UnityEngine.Debug.Log(message);
                else
                    UnityEngine.Debug.LogError(message);
                serializedObject.Update();
            }

            if (GUILayout.Button("Export World Templates"))
            {
                serializedObject.ApplyModifiedProperties();
                if (WorldTemplateAssetExporter.TryExport(out string outputPath, out string errorMessage))
                    UnityEngine.Debug.Log($"Exported world_templates.json to {outputPath}.");
                else
                    UnityEngine.Debug.LogError(errorMessage);
                serializedObject.Update();
            }

            EditorGUILayout.EndHorizontal();

            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Sort/Clean"))
            {
                serializedObject.ApplyModifiedProperties();
                WorldTemplateDatabase catalog = (WorldTemplateDatabase)target;
                Undo.RecordObject(catalog, "Sort World Template Database");
                catalog.SortAndClean();
                EditorUtility.SetDirty(catalog);
                serializedObject.Update();
            }

            if (GUILayout.Button("Reload Inspector"))
                Repaint();

            EditorGUILayout.EndHorizontal();
            EditorGUILayout.Space();
        }

        private static void DrawReferenceWarnings(WorldTemplateDatabase database)
        {
            EditorGUILayout.LabelField("Reference Validation", EditorStyles.boldLabel);

            WorldArchetypeDatabase archetypeDatabase = WorldArchetypeDatabaseImporter.GetSelectedOrDefaultDatabase();
            if (archetypeDatabase == null)
            {
                EditorGUILayout.HelpBox("WorldArchetypeDatabase could not be resolved, so template usage validation is unavailable.", MessageType.Info);
                EditorGUILayout.Space();
                return;
            }

            IReadOnlyList<WorldTemplateData> templates = database != null ? database.Entries : null;
            IReadOnlyList<WorldArchetypeData> archetypes = archetypeDatabase.Entries;
            bool emitted = false;

            if (templates != null)
            {
                for (int i = 0; i < templates.Count; ++i)
                {
                    WorldTemplateData template = templates[i];
                    if (template == null)
                        continue;

                    string templateName = template.AssetName?.Trim() ?? string.Empty;
                    if (string.IsNullOrWhiteSpace(templateName))
                        continue;

                    bool referenced = false;
                    if (archetypes != null)
                    {
                        for (int j = 0; j < archetypes.Count; ++j)
                        {
                            WorldArchetypeData archetype = archetypes[j];
                            if (archetype == null)
                                continue;

                            if (string.Equals(archetype.TemplateName, templateName, System.StringComparison.Ordinal))
                            {
                                referenced = true;
                                break;
                            }
                        }
                    }

                    if (!referenced)
                    {
                        EditorGUILayout.HelpBox($"World template '{templateName}' is not referenced by any world archetype.", MessageType.Warning);
                        emitted = true;
                    }
                }
            }

            if (!emitted)
                EditorGUILayout.HelpBox("All world templates are referenced by at least one world archetype.", MessageType.Info);

            EditorGUILayout.Space();
        }
    }
} // namespace JamUnity.Editor.World
