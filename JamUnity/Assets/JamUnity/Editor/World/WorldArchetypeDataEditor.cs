using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.World;


namespace JamUnity.Editor.World
{
    [CustomEditor(typeof(WorldArchetypeData))]
    public sealed class WorldArchetypeDataEditor : UnityEditor.Editor
    {
        private SerializedProperty assetNameProp;
        private SerializedProperty worldTemplateProp;
        private SerializedProperty actorArchetypeDatabaseProp;
        private SerializedProperty actorLevelNameProp;
        private SerializedProperty physicsAssetDatabaseProp;

        private void OnEnable()
        {
            assetNameProp              = serializedObject.FindProperty("assetName");
            worldTemplateProp          = serializedObject.FindProperty("worldTemplate");
            actorArchetypeDatabaseProp = serializedObject.FindProperty("actorArchetypeDatabase");
            actorLevelNameProp         = serializedObject.FindProperty("actorLevelName");
            physicsAssetDatabaseProp   = serializedObject.FindProperty("physicsAssetDatabase");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.PropertyField(assetNameProp, new GUIContent("Asset Name"));

            EditorGUILayout.Space(4f);
            EditorGUILayout.LabelField("Authoring References", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(worldTemplateProp, new GUIContent("World Template"));

            EditorGUILayout.Space(6f);
            EditorGUILayout.PropertyField(actorArchetypeDatabaseProp, new GUIContent("Actor Archetype Database"));
            EditorGUILayout.PropertyField(actorLevelNameProp, new GUIContent("Actor Level Name"));
            EditorGUILayout.PropertyField(physicsAssetDatabaseProp, new GUIContent("Physics Asset Database"));

            serializedObject.ApplyModifiedProperties();
        }
    }
} // namespace JamUnity.Editor.World
