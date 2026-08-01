using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.Actor;
using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Actor
{
    [CustomEditor(typeof(ActorArchetypeDatabase))]
    public sealed class ActorArchetypeDatabaseEditor : UnityEditor.Editor
    {
        private SerializedProperty sharedDataPathProp;
        private SerializedProperty sharedDataVersionProp;
        private SerializedProperty archetypesProp;
    
        private bool showEntries = true;
    
        private void OnEnable()
        {
            sharedDataPathProp    = serializedObject.FindProperty("sharedDataPath");
            sharedDataVersionProp = serializedObject.FindProperty("sharedDataVersion");
            archetypesProp        = serializedObject.FindProperty("archetypes");
        }
    
        public override void OnInspectorGUI()
        {
            serializedObject.Update();
    
            EditorGUILayout.HelpBox(
                "ActorArchetypeDatabase is the Unity-side authoring index for actor_archetypes.json. Shared rows are exported from AssetName and PhysicsArchetype reference.",
                MessageType.Info);
    
            DrawActions();
            DrawConfiguration();
            DrawEntries();
            DrawWarnings();
    
            serializedObject.ApplyModifiedProperties();
        }
    
        private void DrawActions()
        {
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Sync From Shared"))
                    InvokeDatabaseMutation("Sync Actor Archetypes", ActorArchetypeDatabaseImporter.TryImport);
    
                if (GUILayout.Button("Write To Shared"))
                    InvokeDatabaseMutation("Write Actor Archetypes", ActorArchetypeDatabaseExporter.TryExport);
            }
    
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Validate"))
                {
                    ActorArchetypeDatabase database = (ActorArchetypeDatabase)target;
                    if (ActorArchetypeDatabaseExporter.TryValidate(database, out string message))
                        Debug.Log(message);
                    else
                        Debug.LogError(message);
                }
            }
    
            EditorGUILayout.Space();
        }
    
        private void DrawConfiguration()
        {
            EditorGUILayout.LabelField("Configuration", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(sharedDataPathProp, new GUIContent("Manifest-Relative File"));
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextField("Database Asset Path", ActorArchetypeDatabase.DatabaseAssetPath);
                EditorGUILayout.TextField("Asset Data Root", ActorArchetypeDatabase.ActorArchetypeDataRoot);
                EditorGUILayout.PropertyField(sharedDataVersionProp, new GUIContent("Shared Data Version"));
            }
            EditorGUILayout.Space();
        }
    
        private void DrawEntries()
        {
            if (archetypesProp == null)
                return;
    
            showEntries = EditorGUILayout.Foldout(showEntries, "Actor Archetypes", true);
            if (!showEntries)
                return;
    
            for (int i = 0; i < archetypesProp.arraySize; ++i)
            {
                SerializedProperty archetypeProp = archetypesProp.GetArrayElementAtIndex(i);
                DrawEntry(archetypeProp, i);
            }
    
            using (new EditorGUILayout.HorizontalScope())
            {
                GUILayout.FlexibleSpace();
                if (GUILayout.Button("Add Entry", GUILayout.Width(120)))
                    archetypesProp.InsertArrayElementAtIndex(archetypesProp.arraySize);
            }
    
            EditorGUILayout.Space();
        }
    
        private void DrawWarnings()
        {
            ActorArchetypeDatabase database = (ActorArchetypeDatabase)target;
            if (!database.TryValidateEntries(out var errors))
            {
                EditorGUILayout.LabelField("Warnings", EditorStyles.boldLabel);
                for (int i = 0; i < errors.Count; ++i)
                    EditorGUILayout.HelpBox(errors[i], MessageType.Warning);
                EditorGUILayout.Space();
            }
        }
    
        private void DrawEntry(SerializedProperty archetypeProp, int index)
        {
            ActorArchetypeData archetype = archetypeProp.objectReferenceValue as ActorArchetypeData;
    
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField($"Entry {index}", EditorStyles.boldLabel);
            if (GUILayout.Button("Remove", GUILayout.Width(70)))
            {
                archetypesProp.DeleteArrayElementAtIndex(index);
                EditorGUILayout.EndHorizontal();
                EditorGUILayout.EndVertical();
                return;
            }
            EditorGUILayout.EndHorizontal();
    
            EditorGUILayout.PropertyField(archetypeProp, new GUIContent("Archetype Data"));
    
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextField("Asset Name", archetype != null ? archetype.AssetName : string.Empty);
                EditorGUILayout.ObjectField("Resolved Physics Archetype", archetype != null ? archetype.PhysicsArchetype : null, typeof(PhysicsArchetypeData), false);
            }
    
            EditorGUILayout.EndVertical();
        }
    
        private void InvokeDatabaseMutation(string undoLabel, MutationFunc mutation)
        {
            serializedObject.ApplyModifiedProperties();
    
            ActorArchetypeDatabase database = (ActorArchetypeDatabase)target;
            Undo.RecordObject(database, undoLabel);
            if (mutation(database, out string message))
            {
                EditorUtility.SetDirty(database);
            }
            else if (!string.IsNullOrWhiteSpace(message))
            {
                Debug.LogError(message);
            }
    
            serializedObject.Update();
        }
    
        private delegate bool MutationFunc(ActorArchetypeDatabase database, out string message);
    }

    
    
} // namespace JamUnity.Editor.Actor
