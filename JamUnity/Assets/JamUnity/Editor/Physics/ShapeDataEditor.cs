using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Physics
{
    [CustomEditor(typeof(ShapeData))]
    public sealed class ShapeDataEditor : UnityEditor.Editor
    {
        private SerializedProperty assetNameProp;
        private SerializedProperty typeProp;
        private SerializedProperty localPositionProp;
        private SerializedProperty localRotationProp;
        private SerializedProperty materialProp;
        private SerializedProperty shapeFlagProp;
        private SerializedProperty simFilterProp;
        private SerializedProperty qryFilterProp;
        private SerializedProperty contactOffsetProp;
        private SerializedProperty restOffsetProp;
        private SerializedProperty halfExtentsProp;
        private SerializedProperty radiusProp;
        private SerializedProperty halfHeightProp;
        private SerializedProperty meshProp;

        private void OnEnable()
        {
            assetNameProp       = serializedObject.FindProperty("assetName");
            typeProp            = serializedObject.FindProperty("type");
            localPositionProp   = serializedObject.FindProperty("localPosition");
            localRotationProp   = serializedObject.FindProperty("localRotation");
            materialProp        = serializedObject.FindProperty("material");
            shapeFlagProp       = serializedObject.FindProperty("shapeFlag");
            simFilterProp       = serializedObject.FindProperty("simFilter");
            qryFilterProp       = serializedObject.FindProperty("qryFilter");
            contactOffsetProp   = serializedObject.FindProperty("contactOffset");
            restOffsetProp      = serializedObject.FindProperty("restOffset");
            halfExtentsProp     = serializedObject.FindProperty("halfExtents");
            radiusProp          = serializedObject.FindProperty("radius");
            halfHeightProp      = serializedObject.FindProperty("halfHeight");
            meshProp            = serializedObject.FindProperty("mesh");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            var authoring = (ShapeData)target;

            EditorGUILayout.PropertyField(assetNameProp, new GUIContent("Asset Name"));
            EditorGUILayout.PropertyField(typeProp);
            EditorGUILayout.PropertyField(localPositionProp);
            EditorGUILayout.PropertyField(localRotationProp);
            PhysicsDataEditorUtil.DrawObjectFieldWithCreate<MaterialData>(materialProp, PhysicsDataEditorUtil.MaterialsFolder, "PhysicsMaterial");
            EditorGUILayout.PropertyField(shapeFlagProp);
            EditorGUILayout.PropertyField(simFilterProp, includeChildren: true);
            EditorGUILayout.PropertyField(qryFilterProp, includeChildren: true);
            EditorGUILayout.PropertyField(contactOffsetProp);
            EditorGUILayout.PropertyField(restOffsetProp);

            var shapeType = (eShapeType)typeProp.intValue;
            switch (shapeType)
            {
                case eShapeType.Box:
                    EditorGUILayout.PropertyField(halfExtentsProp);
                    break;
                case eShapeType.Sphere:
                    EditorGUILayout.PropertyField(radiusProp);
                    break;
                case eShapeType.Capsule:
                    EditorGUILayout.PropertyField(radiusProp);
                    EditorGUILayout.PropertyField(halfHeightProp);
                    break;
                case eShapeType.TriangleMesh:
                case eShapeType.ConvexMesh:
                    PhysicsDataEditorUtil.DrawObjectFieldWithCreate<MeshData>(meshProp, PhysicsDataEditorUtil.MeshesFolder, "PhysicsMesh");
                    if (authoring.Mesh == null)
                        EditorGUILayout.HelpBox("Mesh shape에는 Mesh가 필요합니다.", MessageType.Warning);
                    break;
            }

            serializedObject.ApplyModifiedProperties();
        }
    }
} // namespace JamUnity.Editor.Physics
