using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.World;

namespace JamUnity.Editor.World
{
    [CustomEditor(typeof(WorldTemplateData))]
    public sealed class WorldTemplateDataEditor : UnityEditor.Editor
    {
        private SerializedProperty assetNameProp;
        private SerializedProperty groupProp;
        private SerializedProperty allowMultipleInstancePerUserProp;
        private SerializedProperty persistentProp;
        private SerializedProperty destroyWhenEmptyProp;
        private SerializedProperty isPrivateProp;
        private SerializedProperty capacityProp;

        private void OnEnable()
        {
            assetNameProp                        = serializedObject.FindProperty("assetName");
            groupProp                            = serializedObject.FindProperty("group");
            allowMultipleInstancePerUserProp     = serializedObject.FindProperty("allowMultipleInstancePerUser");
            persistentProp                       = serializedObject.FindProperty("persistent");
            destroyWhenEmptyProp                 = serializedObject.FindProperty("destroyWhenEmpty");
            isPrivateProp                        = serializedObject.FindProperty("isPrivate");
            capacityProp                         = serializedObject.FindProperty("capacity");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.PropertyField(assetNameProp, new GUIContent("Asset Name"));

            EditorGUILayout.Space(4f);
            EditorGUILayout.LabelField("Runtime", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(groupProp);
            EditorGUILayout.PropertyField(capacityProp);

            EditorGUILayout.Space(6f);
            EditorGUILayout.LabelField("Lifecycle", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(allowMultipleInstancePerUserProp);
            EditorGUILayout.PropertyField(persistentProp);
            EditorGUILayout.PropertyField(destroyWhenEmptyProp);
            EditorGUILayout.PropertyField(isPrivateProp);
            serializedObject.ApplyModifiedProperties();
        }
    }
} // namespace JamUnity.Editor.World.Template
