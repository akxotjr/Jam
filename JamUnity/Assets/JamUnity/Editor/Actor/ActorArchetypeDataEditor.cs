using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.Actor;

namespace JamUnity.Editor.Actor
{
    [CustomEditor(typeof(ActorArchetypeData))]
    public sealed class ActorArchetypeDataEditor : UnityEditor.Editor
    {
        private SerializedProperty assetNameProp;
        private SerializedProperty physicsArchetypeProp;
        private SerializedProperty spawnPolicyProp;
        private SerializedProperty allowReplicationProp;

        private void OnEnable()
        {
            assetNameProp            = serializedObject.FindProperty("assetName");
            physicsArchetypeProp     = serializedObject.FindProperty("physicsArchetype");
            spawnPolicyProp          = serializedObject.FindProperty("spawnPolicy");
            allowReplicationProp     = serializedObject.FindProperty("allowReplication");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("Sync From Shared"))
            {
                if (!ActorArchetypeDatabaseImporter.TryImport(out string message))
                    Debug.LogError(message);
            }
            if (GUILayout.Button("Write To Shared"))
            {
                if (!ActorArchetypeDatabaseExporter.TryExport(out string message))
                    Debug.LogError(message);
            }
            if (GUILayout.Button("Validate"))
            {
                if (ActorArchetypeDatabaseExporter.TryValidate(out string message))
                    Debug.Log(message);
                else
                    Debug.LogError(message);
            }
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.Space();

            EditorGUILayout.PropertyField(assetNameProp, new GUIContent("Asset Name"));
            EditorGUILayout.PropertyField(physicsArchetypeProp, new GUIContent("Physics Archetype"));
            EditorGUILayout.PropertyField(spawnPolicyProp, new GUIContent("Spawn Policy"));
            EditorGUILayout.PropertyField(allowReplicationProp, new GUIContent("Allow Replication"));
            EditorGUILayout.Space(8f);
            EditorGUILayout.HelpBox(
                "Actor prefab bindings are owned by the global Unity-only ActorPresentationCatalog.",
                MessageType.Info);

            serializedObject.ApplyModifiedProperties();
        }
    }
    
} // namespace JamUnity.Editor.Actor
