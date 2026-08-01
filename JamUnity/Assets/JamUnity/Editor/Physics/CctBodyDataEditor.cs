using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Physics
{
    [CustomEditor(typeof(CctBodyData))]
    public sealed class CctBodyDataEditor : UnityEditor.Editor
    {
        private SerializedProperty assetNameProp;
        private SerializedProperty radiusProp;
        private SerializedProperty heightProp;
        private SerializedProperty materialProp;
        private SerializedProperty densityProp;
        private SerializedProperty simFilterProp;
        private SerializedProperty qryFilterProp;
        private SerializedProperty slopeLimitProp;
        private SerializedProperty invisibleWallHeightProp;
        private SerializedProperty maxJumpHeightProp;
        private SerializedProperty contactOffsetProp;
        private SerializedProperty stepOffsetProp;
        private SerializedProperty scaleCoeffProp;
        private SerializedProperty volumeGrowthProp;

        private void OnEnable()
        {
            assetNameProp           = serializedObject.FindProperty("assetName");
            radiusProp              = serializedObject.FindProperty("radius");
            heightProp              = serializedObject.FindProperty("height");
            materialProp            = serializedObject.FindProperty("material");
            densityProp             = serializedObject.FindProperty("density");
            simFilterProp           = serializedObject.FindProperty("simFilter");
            qryFilterProp           = serializedObject.FindProperty("qryFilter");
            slopeLimitProp          = serializedObject.FindProperty("slopeLimit");
            invisibleWallHeightProp = serializedObject.FindProperty("invisibleWallHeight");
            maxJumpHeightProp       = serializedObject.FindProperty("maxJumpHeight");
            contactOffsetProp       = serializedObject.FindProperty("contactOffset");
            stepOffsetProp          = serializedObject.FindProperty("stepOffset");
            scaleCoeffProp          = serializedObject.FindProperty("scaleCoeff");
            volumeGrowthProp        = serializedObject.FindProperty("volumeGrowth");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            var authoring = (CctBodyData)target;

            EditorGUILayout.PropertyField(assetNameProp, new GUIContent("Asset Name"));
            EditorGUILayout.PropertyField(radiusProp);
            EditorGUILayout.PropertyField(heightProp);
            PhysicsDataEditorUtil.DrawObjectFieldWithCreate<MaterialData>(materialProp, PhysicsDataEditorUtil.MaterialsFolder, "CharacterMaterial");
            if (authoring.Material == null)
                EditorGUILayout.HelpBox("CctBody에는 Material이 필요합니다.", MessageType.Warning);
            EditorGUILayout.PropertyField(densityProp);
            EditorGUILayout.PropertyField(simFilterProp, includeChildren: true);
            EditorGUILayout.PropertyField(qryFilterProp, includeChildren: true);
            EditorGUILayout.PropertyField(slopeLimitProp);
            EditorGUILayout.PropertyField(invisibleWallHeightProp);
            EditorGUILayout.PropertyField(maxJumpHeightProp);
            EditorGUILayout.PropertyField(contactOffsetProp);
            EditorGUILayout.PropertyField(stepOffsetProp);
            EditorGUILayout.PropertyField(scaleCoeffProp);
            EditorGUILayout.PropertyField(volumeGrowthProp);

            serializedObject.ApplyModifiedProperties();
        }
    }

} // namespace JamUnity.Editor.Physics
