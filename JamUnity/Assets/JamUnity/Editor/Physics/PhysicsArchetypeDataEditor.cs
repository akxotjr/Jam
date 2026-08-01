using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Physics
{
    [CustomEditor(typeof(PhysicsArchetypeData))]
    public sealed class PhysicsArchetypeDataEditor : UnityEditor.Editor
    {
        private SerializedProperty assetNameProp;
        private SerializedProperty actorTypeProp;
        private SerializedProperty bodyTypeProp;
        private SerializedProperty motionTypeProp;
        private SerializedProperty motionFlagsProp;
        private SerializedProperty rigidShapesProp;
        private SerializedProperty dynamicBodyProp;
        private SerializedProperty behaviorKindProp;
        private SerializedProperty kinematicBehaviorProp;
        private SerializedProperty projectileBehaviorProp;
        private SerializedProperty cctBodyProp;
        private SerializedProperty hitboxesProp;
        private SerializedProperty controllerTypeProp;
        private SerializedProperty moveConfigProp;

        private void OnEnable()
        {
            assetNameProp           = serializedObject.FindProperty("assetName");
            actorTypeProp           = serializedObject.FindProperty("actorType");
            bodyTypeProp            = serializedObject.FindProperty("bodyType");
            motionTypeProp          = serializedObject.FindProperty("motionType");
            motionFlagsProp         = serializedObject.FindProperty("motionFlags");
            rigidShapesProp         = serializedObject.FindProperty("rigidShapes");
            dynamicBodyProp         = serializedObject.FindProperty("dynamicBody");
            behaviorKindProp        = serializedObject.FindProperty("behaviorKind");
            kinematicBehaviorProp   = serializedObject.FindProperty("kinematicBehavior");
            projectileBehaviorProp  = serializedObject.FindProperty("projectileBehavior");
            cctBodyProp             = serializedObject.FindProperty("cctBody");
            hitboxesProp            = serializedObject.FindProperty("hitboxes");
            controllerTypeProp      = serializedObject.FindProperty("controllerType");
            moveConfigProp          = serializedObject.FindProperty("moveConfig");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            PhysicsArchetypeData physicsArchetype = (PhysicsArchetypeData)target;
            EditorGUILayout.PropertyField(assetNameProp, new GUIContent("Asset Name"));
            EditorGUILayout.PropertyField(actorTypeProp);
            EditorGUILayout.PropertyField(bodyTypeProp);
            EditorGUILayout.PropertyField(motionTypeProp);
            motionFlagsProp.intValue = (int)(eMotionFlag)EditorGUILayout.EnumFlagsField("Motion Flags", (eMotionFlag)motionFlagsProp.intValue);

            EditorGUILayout.Space(6f);
            if ((eBodyType)bodyTypeProp.intValue == eBodyType.RigidBody)
                DrawRigidSection(physicsArchetype);
            else
                DrawCharacterSection(physicsArchetype);

            serializedObject.ApplyModifiedProperties();
        }

        private void DrawRigidSection(PhysicsArchetypeData physicsArchetype)
        {
            EditorGUILayout.LabelField("Rigid Body", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(rigidShapesProp, includeChildren: true);
            PhysicsDataEditorUtil.DrawCreateAndAddButton<ShapeData>(rigidShapesProp, PhysicsDataEditorUtil.ShapesFolder, $"{physicsArchetype.AssetName}_Shape");
            PhysicsDataEditorUtil.DrawObjectFieldWithCreate<DynamicBodyData>(dynamicBodyProp, PhysicsDataEditorUtil.BodiesFolder, $"{physicsArchetype.AssetName}_DynamicBody");
            EditorGUILayout.PropertyField(behaviorKindProp);

            eRigidBehaviorKind behaviorKind = (eRigidBehaviorKind)behaviorKindProp.intValue;
            switch (behaviorKind)
            {
                case eRigidBehaviorKind.KinematicDriver:
                    PhysicsDataEditorUtil.DrawObjectFieldWithCreate<KinematicDriverConfigData>(kinematicBehaviorProp, PhysicsDataEditorUtil.BehaviorsFolder, $"{physicsArchetype.AssetName}_Kinematic");
                    break;
                case eRigidBehaviorKind.Projectile:
                    PhysicsDataEditorUtil.DrawObjectFieldWithCreate<ProjectileConfigData>(projectileBehaviorProp, PhysicsDataEditorUtil.BehaviorsFolder, $"{physicsArchetype.AssetName}_Projectile");
                    break;
            }
        }

        private void DrawCharacterSection(PhysicsArchetypeData physicsArchetype)
        {
            EditorGUILayout.LabelField("Character Body", EditorStyles.boldLabel);
            PhysicsDataEditorUtil.DrawObjectFieldWithCreate<CctBodyData>(cctBodyProp, PhysicsDataEditorUtil.BodiesFolder, $"{physicsArchetype.AssetName}_CctBody");
            EditorGUILayout.PropertyField(hitboxesProp, includeChildren: true);
            PhysicsDataEditorUtil.DrawCreateAndAddButton<ShapeData>(hitboxesProp, PhysicsDataEditorUtil.ShapesFolder, $"{physicsArchetype.AssetName}_Hitbox");
            EditorGUILayout.PropertyField(controllerTypeProp);
            PhysicsDataEditorUtil.DrawObjectFieldWithCreate<CharacterMoveConfigData>(moveConfigProp, PhysicsDataEditorUtil.MoveConfigsFolder, $"{physicsArchetype.AssetName}_MoveConfig");
        }
    }
} // namespace JamUnity.Editor.Physics
