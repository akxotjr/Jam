using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.World;

namespace JamUnity.Editor.World
{
    [CustomEditor(typeof(WorldAuthoring))]
    public sealed class WorldAuthoringEditor : UnityEditor.Editor
    {
        public override void OnInspectorGUI()
        {
            serializedObject.Update();
            EditorGUILayout.PropertyField(serializedObject.FindProperty("worldArchetype"));
            EditorGUILayout.PropertyField(serializedObject.FindProperty("worldRoot"));

            WorldAuthoring authoring = (WorldAuthoring)target;
            if (authoring.WorldArchetype == null)
                EditorGUILayout.HelpBox("WorldAuthoring requires a WorldArchetypeData reference.", MessageType.Warning);
            if (authoring.WorldRoot == null)
                EditorGUILayout.HelpBox("WorldAuthoring requires WorldRoot on the same prefab root.", MessageType.Warning);

            serializedObject.ApplyModifiedProperties();
        }
    }
}
