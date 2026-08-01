using UnityEditor;
using UnityEngine;

using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Physics
{
    [CustomEditor(typeof(KinematicDriverConfigData))]
    public sealed class KinematicDriverConfigDataEditor : UnityEditor.Editor
    {
        private SerializedProperty assetNameProp;
        private SerializedProperty computeDerivedVelProp;
        private SerializedProperty carryRidersProp;
        private SerializedProperty sweepProp;
        private SerializedProperty maxSpeedProp;
        private SerializedProperty sourceTypeProp;
        private SerializedProperty speedProp;
        private SerializedProperty durationProp;
        private SerializedProperty loopProp;
        private SerializedProperty buildSegmentsProp;
        private SerializedProperty useEaseProfileProp;
        private SerializedProperty easeTypeProp;
        private SerializedProperty loopModeProp;
        private SerializedProperty alphaProp;
        private SerializedProperty curveTypeProp;
        private SerializedProperty waypointsProp;
        private SerializedProperty controlPointsProp;
        private SerializedProperty orbitProp;
        private SerializedProperty followProp;
        private SerializedProperty networkPoseComputeDerivedVelocityProp;

        private void OnEnable()
        {
            assetNameProp                         = serializedObject.FindProperty("assetName");
            computeDerivedVelProp                 = serializedObject.FindProperty("computeDerivedVel");
            carryRidersProp                       = serializedObject.FindProperty("carryRiders");
            sweepProp                             = serializedObject.FindProperty("sweep");
            maxSpeedProp                          = serializedObject.FindProperty("maxSpeed");
            sourceTypeProp                        = serializedObject.FindProperty("sourceType");
            speedProp                             = serializedObject.FindProperty("speed");
            durationProp                          = serializedObject.FindProperty("duration");
            loopProp                              = serializedObject.FindProperty("loop");
            buildSegmentsProp                     = serializedObject.FindProperty("buildSegments");
            useEaseProfileProp                    = serializedObject.FindProperty("useEaseProfile");
            easeTypeProp                          = serializedObject.FindProperty("easeType");
            loopModeProp                          = serializedObject.FindProperty("loopMode");
            alphaProp                             = serializedObject.FindProperty("alpha");
            curveTypeProp                         = serializedObject.FindProperty("curveType");
            waypointsProp                         = serializedObject.FindProperty("waypoints");
            controlPointsProp                     = serializedObject.FindProperty("controlPoints");
            orbitProp                             = serializedObject.FindProperty("orbit");
            followProp                            = serializedObject.FindProperty("follow");
            networkPoseComputeDerivedVelocityProp = serializedObject.FindProperty("networkPoseComputeDerivedVelocity");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            var authoring = (KinematicDriverConfigData)target;

            EditorGUILayout.PropertyField(assetNameProp, new GUIContent("Asset Name"));

            EditorGUILayout.Space(4f);
            EditorGUILayout.LabelField("Common", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(computeDerivedVelProp);
            EditorGUILayout.PropertyField(carryRidersProp);
            EditorGUILayout.PropertyField(sweepProp);
            EditorGUILayout.PropertyField(maxSpeedProp);

            EditorGUILayout.Space(6f);
            EditorGUILayout.LabelField("Source", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(sourceTypeProp);
            EditorGUILayout.PropertyField(speedProp);
            EditorGUILayout.PropertyField(durationProp);
            EditorGUILayout.PropertyField(loopProp);
            EditorGUILayout.PropertyField(buildSegmentsProp);
            EditorGUILayout.PropertyField(useEaseProfileProp);
            if (useEaseProfileProp.boolValue)
                EditorGUILayout.PropertyField(easeTypeProp);
            EditorGUILayout.PropertyField(loopModeProp);

            var sourceType = (eKinematicSourceType)sourceTypeProp.intValue;
            switch (sourceType)
            {
                case eKinematicSourceType.Waypoint:
                    EditorGUILayout.PropertyField(waypointsProp, includeChildren: true);
                    if (authoring.Waypoints == null || authoring.Waypoints.Count == 0)
                        EditorGUILayout.HelpBox("Waypoint source에는 waypoint가 하나 이상 필요합니다.", MessageType.Warning);
                    break;
                case eKinematicSourceType.Curve:
                    EditorGUILayout.PropertyField(alphaProp);
                    EditorGUILayout.PropertyField(curveTypeProp);
                    EditorGUILayout.PropertyField(controlPointsProp, includeChildren: true);
                    if (authoring.ControlPoints == null || authoring.ControlPoints.Count == 0)
                        EditorGUILayout.HelpBox("Curve source에는 control point가 하나 이상 필요합니다.", MessageType.Warning);
                    break;
                case eKinematicSourceType.Orbit:
                    EditorGUILayout.PropertyField(orbitProp, includeChildren: true);
                    break;
                case eKinematicSourceType.Follow:
                    EditorGUILayout.PropertyField(followProp, includeChildren: true);
                    break;
                case eKinematicSourceType.NetworkPose:
                    EditorGUILayout.PropertyField(networkPoseComputeDerivedVelocityProp);
                    break;
            }

            serializedObject.ApplyModifiedProperties();
        }
    }
} // namespace JamUnity.Editor.Physics
