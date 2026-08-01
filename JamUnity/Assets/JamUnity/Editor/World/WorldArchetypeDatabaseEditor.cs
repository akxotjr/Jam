using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

using JamUnity.World.Presentation;

using JamUnity.Authoring.World;


namespace JamUnity.Editor.World
{
    [CustomEditor(typeof(WorldArchetypeDatabase))]
    public sealed class WorldArchetypeDatabaseEditor : UnityEditor.Editor
    {
        private const string DefaultWorldPresentationDatabasePath = JamUnity.Core.Util.Path.WorldPresentationDatabaseAssetPath;

        private SerializedProperty worldArchetypesAssetPathProp;
        private SerializedProperty versionProp;
        private SerializedProperty entriesProp;
        private bool showEntries = true;

        private void OnEnable()
        {
            worldArchetypesAssetPathProp = serializedObject.FindProperty("worldArchetypesAssetPath");
            versionProp = serializedObject.FindProperty("version");
            entriesProp = serializedObject.FindProperty("entries");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            WorldArchetypeDatabase database = (WorldArchetypeDatabase)target;

            EditorGUILayout.HelpBox(
                "WorldArchetypeDatabase is the Unity-side cache/editor surface for world_archetypes.json. Related Unity cache assets are authored as references and exported as JSON names.",
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
            EditorGUILayout.PropertyField(worldArchetypesAssetPathProp, new GUIContent("Manifest-Relative File"));
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextField("Database Asset Path", WorldArchetypeDatabase.DatabaseAssetPath);
                EditorGUILayout.TextField("Asset Data Root", WorldArchetypeDatabase.WorldArchetypeDataRoot);
                EditorGUILayout.PropertyField(versionProp, new GUIContent("Version"));
            }
            EditorGUILayout.Space();
        }

        private void DrawEntries()
        {
            if (entriesProp == null)
                return;

            showEntries = EditorGUILayout.Foldout(showEntries, "World Archetypes", true);
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

        private void DrawEntry(SerializedProperty archetypeProp, int index)
        {
            WorldArchetypeData archetype = archetypeProp.objectReferenceValue as WorldArchetypeData;

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

                EditorGUILayout.PropertyField(archetypeProp, new GUIContent("Archetype Data"));
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextField("Asset Name", archetype != null ? archetype.AssetName : string.Empty);
                EditorGUILayout.TextField("Template Name", archetype != null ? archetype.TemplateName : string.Empty);
            }

            EditorGUILayout.EndVertical();
        }

        private void DrawActionButtons()
        {
            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Import World Archetypes"))
            {
                serializedObject.ApplyModifiedProperties();
                if (WorldArchetypeDatabaseImporter.TryImport(out string message))
                    Debug.Log(message);
                else
                    Debug.LogError(message);
                serializedObject.Update();
            }

            if (GUILayout.Button("Export World Archetypes"))
            {
                serializedObject.ApplyModifiedProperties();
                if (WorldArchetypeDatabaseExporter.TryExport(out string outputPath, out string errorMessage))
                    Debug.Log($"Exported world_archetypes.json to {outputPath}.");
                else
                    Debug.LogError(errorMessage);
                serializedObject.Update();
            }

            EditorGUILayout.EndHorizontal();

            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Sort/Clean"))
            {
                serializedObject.ApplyModifiedProperties();
                WorldArchetypeDatabase database = (WorldArchetypeDatabase)target;
                Undo.RecordObject(database, "Sort World Archetype Database");
                database.SortAndClean();
                EditorUtility.SetDirty(database);
                serializedObject.Update();
            }

            if (GUILayout.Button("Reload Inspector"))
                Repaint();

            EditorGUILayout.EndHorizontal();
            EditorGUILayout.Space();
        }

        private static void DrawReferenceWarnings(WorldArchetypeDatabase database)
        {
            EditorGUILayout.LabelField("Reference Validation", EditorStyles.boldLabel);

            WorldTemplateDatabase templateDatabase = WorldTemplateAssetImporter.GetSelectedOrDefaultDatabase();
            WorldPresentationDatabase presentationDatabase = AssetDatabase.LoadAssetAtPath<WorldPresentationDatabase>(DefaultWorldPresentationDatabasePath);
            if (templateDatabase == null)
            {
                EditorGUILayout.HelpBox("WorldTemplateDatabase could not be resolved, so template_name validation is unavailable.", MessageType.Info);
            }

            IReadOnlyList<WorldArchetypeData> archetypes = database != null ? database.Entries : null;
            bool emitted = false;
            bool usesActorLevelName = false;

            if (archetypes != null)
            {
                for (int i = 0; i < archetypes.Count; ++i)
                {
                    WorldArchetypeData archetype = archetypes[i];
                    if (archetype == null)
                        continue;

                    string archetypeName = archetype.AssetName?.Trim() ?? string.Empty;
                    string templateName = archetype.TemplateName;
                    if (string.IsNullOrWhiteSpace(archetypeName))
                        continue;

                    if (string.IsNullOrWhiteSpace(templateName))
                    {
                        EditorGUILayout.HelpBox($"World archetype '{archetypeName}' is missing template_name.", MessageType.Warning);
                        emitted = true;
                    }
                    else if (templateDatabase != null)
                    {
                        bool hasTemplate = templateDatabase.TryGetTemplate(templateName, out WorldTemplateData template);
                        if (!hasTemplate || template == null)
                        {
                            EditorGUILayout.HelpBox($"World archetype '{archetypeName}' references missing template_name '{templateName}'.", MessageType.Warning);
                            emitted = true;
                        }
                    }

                    if (presentationDatabase == null)
                    {
                        EditorGUILayout.HelpBox("WorldPresentationDatabase could not be resolved, so world prefab binding validation is unavailable.", MessageType.Info);
                        emitted = true;
                    }
                    else
                    {
                        bool hasBinding = presentationDatabase.TryGetEntry(archetype.AssetName, out WorldPresentationDatabase.Entry binding);
                        if (!hasBinding || binding == null)
                        {
                            EditorGUILayout.HelpBox($"World archetype '{archetypeName}' is missing a world presentation binding.", MessageType.Warning);
                            emitted = true;
                        }
                        else if (binding.WorldPrefab == null)
                        {
                            EditorGUILayout.HelpBox($"World archetype '{archetypeName}' resolves to an empty world prefab binding.", MessageType.Warning);
                            emitted = true;
                        }
                    }

                    if (archetype.PhysicsAssetDatabase == null)
                    {
                        EditorGUILayout.HelpBox($"World archetype '{archetypeName}' is missing its PhysicsAssetDatabase reference.", MessageType.Warning);
                        emitted = true;
                    }

                    if (!string.IsNullOrWhiteSpace(archetype.ActorLevelName))
                        usesActorLevelName = true;
                }
            }

            if (usesActorLevelName)
            {
                EditorGUILayout.HelpBox("actor_level_name values are present. This field is part of Native world-content assembly and is preserved by Unity authoring, but Unity does not resolve it into a local database.", MessageType.Info);
                emitted = true;
            }

            if (!emitted)
                EditorGUILayout.HelpBox("All world archetype references that currently have Unity-side resolvers resolve successfully.", MessageType.Info);

            EditorGUILayout.Space();
        }
    }
} // namespace JamUnity.Editor.World
