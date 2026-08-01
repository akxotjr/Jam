using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Physics
{
    [CustomEditor(typeof(MeshData))]
    public sealed class MeshDataEditor : UnityEditor.Editor
    {
        private SerializedProperty assetNameProp;
        private SerializedProperty typeProp;
        private SerializedProperty cookedPathProp;
        private SerializedProperty srcPathProp;
        private SerializedProperty srcMeshIndexProp;
        private SerializedProperty srcPrimitiveIndexProp;

        private void OnEnable()
        {
            assetNameProp           = serializedObject.FindProperty("assetName");
            typeProp                = serializedObject.FindProperty("type");
            cookedPathProp          = serializedObject.FindProperty("cookedPath");
            srcPathProp             = serializedObject.FindProperty("srcPath");
            srcMeshIndexProp        = serializedObject.FindProperty("srcMeshIndex");
            srcPrimitiveIndexProp   = serializedObject.FindProperty("srcPrimitiveIndex");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.PropertyField(assetNameProp, new GUIContent("Asset Name"));
            EditorGUILayout.PropertyField(typeProp);
            EditorGUILayout.PropertyField(cookedPathProp);

            EditorGUILayout.Space(6f);
            EditorGUILayout.LabelField("Source Metadata", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(srcPathProp);
            EditorGUILayout.PropertyField(srcMeshIndexProp);
            EditorGUILayout.PropertyField(srcPrimitiveIndexProp);

            serializedObject.ApplyModifiedProperties();
        }
    }
} // namespace JamUnity.Editor.Physics
