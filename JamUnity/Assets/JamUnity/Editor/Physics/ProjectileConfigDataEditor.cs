using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Physics
{
    [CustomEditor(typeof(ProjectileConfigData))]
    public sealed class ProjectileConfigDataEditor : UnityEditor.Editor
    {
        private SerializedProperty assetNameProp;
        private SerializedProperty kindProp;
        private SerializedProperty motionModelProp;
        private SerializedProperty initialVelocityProp;
        private SerializedProperty gravityScaleProp;
        private SerializedProperty hitModelProp;
        private SerializedProperty useShapeSweepProp;
        private SerializedProperty fallbackRaycastProp;
        private SerializedProperty requestFdProp;
        private SerializedProperty maxRangeProp;
        private SerializedProperty maxLifetimeProp;

        private void OnEnable()
        {
            assetNameProp       = serializedObject.FindProperty("assetName");
            kindProp            = serializedObject.FindProperty("kind");
            motionModelProp     = serializedObject.FindProperty("motionModel");
            initialVelocityProp = serializedObject.FindProperty("initialVelocity");
            gravityScaleProp    = serializedObject.FindProperty("gravityScale");
            hitModelProp        = serializedObject.FindProperty("hitModel");
            useShapeSweepProp   = serializedObject.FindProperty("useShapeSweep");
            fallbackRaycastProp = serializedObject.FindProperty("fallbackRaycast");
            requestFdProp       = serializedObject.FindProperty("requestFd");
            maxRangeProp        = serializedObject.FindProperty("maxRange");
            maxLifetimeProp     = serializedObject.FindProperty("maxLifetime");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.PropertyField(assetNameProp, new GUIContent("Asset Name"));
            EditorGUILayout.PropertyField(kindProp);

            EditorGUILayout.Space(4f);
            EditorGUILayout.LabelField("Motion", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(motionModelProp);
            EditorGUILayout.PropertyField(initialVelocityProp);

            var motionModel = (eProjectileMotionModel)motionModelProp.intValue;
            if (motionModel == eProjectileMotionModel.Ballistic)
                EditorGUILayout.PropertyField(gravityScaleProp);

            EditorGUILayout.Space(6f);
            EditorGUILayout.LabelField("Hit", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(hitModelProp);
            EditorGUILayout.PropertyField(useShapeSweepProp);
            EditorGUILayout.PropertyField(fallbackRaycastProp);
            EditorGUILayout.PropertyField(requestFdProp, includeChildren: true);

            EditorGUILayout.Space(6f);
            EditorGUILayout.LabelField("Lifetime", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(maxRangeProp);
            EditorGUILayout.PropertyField(maxLifetimeProp);

            serializedObject.ApplyModifiedProperties();
        }
    }
} // namespace JamUnity.Editor.Physics
