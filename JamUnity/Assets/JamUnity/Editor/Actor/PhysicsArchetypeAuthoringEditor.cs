using UnityEditor;

using JamUnity.Authoring.Physics;
using JamUnity.Editor.Physics;

namespace JamUnity.Editor.Actor
{
    [CustomEditor(typeof(PhysicsArchetypeAuthoring))]
    public sealed class PhysicsArchetypeAuthoringEditor : UnityEditor.Editor
    {
        private SerializedProperty physicsArchetypeProp;
        private UnityEditor.Editor cachedPhysicsArchetypeEditor;

        private void OnEnable()
        {
            physicsArchetypeProp = serializedObject.FindProperty("physicsArchetype");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            PhysicsArchetypeAuthoring authoring = (PhysicsArchetypeAuthoring)target;

            PhysicsDataEditorUtil.DrawObjectFieldWithCreate<PhysicsArchetypeData>(physicsArchetypeProp, PhysicsDataEditorUtil.PhysicsArchetypesFolder, "PhysicsArchetypeData");

            EditorGUILayout.Space(4f);
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextField("Resolved Physics Archetype Name", authoring.PhysicsArchetypeName);
            }

            if (physicsArchetypeProp.objectReferenceValue == null)
            {
                EditorGUILayout.HelpBox("PhysicsArchetypeData를 지정해야 합니다.", MessageType.Warning);
            }
            else
            {
                EditorGUILayout.Space(6f);
                EditorGUILayout.LabelField("Physics Archetype Asset", EditorStyles.boldLabel);
                CreateCachedEditor(physicsArchetypeProp.objectReferenceValue, null, ref cachedPhysicsArchetypeEditor);
                cachedPhysicsArchetypeEditor?.OnInspectorGUI();
            }

            serializedObject.ApplyModifiedProperties();
        }
    }
    
} // namespace JamUnity.Editor.Actor
