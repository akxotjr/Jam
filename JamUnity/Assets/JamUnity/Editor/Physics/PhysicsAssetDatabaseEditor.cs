using UnityEditor;
using UnityEngine;

using JamUnity.Core.Data;
using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Physics
{
    [CustomEditor(typeof(PhysicsAssetDatabase))]
    public sealed class PhysicsAssetDatabaseEditor : UnityEditor.Editor
    {
        private SerializedProperty sharedDataPathProp;
        private SerializedProperty versionProp;
        private SerializedProperty materialsProp;
        private SerializedProperty meshesProp;
        private SerializedProperty shapesProp;
        private SerializedProperty dynamicBodiesProp;
        private SerializedProperty cctBodiesProp;
        private SerializedProperty characterMoveConfigsProp;
        private SerializedProperty kinematicDriverConfigsProp;
        private SerializedProperty projectileConfigsProp;
        private SerializedProperty physicsArchetypesProp;

        private bool showMaterials = true;
        private bool showMeshes;
        private bool showShapes;
        private bool showDynamicBodies;
        private bool showCctBodies;
        private bool showMoveConfigs;
        private bool showKinematicConfigs;
        private bool showProjectileConfigs;
        private bool showPhysicsArchetypes = true;

        private void OnEnable()
        {
            sharedDataPathProp          = serializedObject.FindProperty("sharedDataPath");
            versionProp                 = serializedObject.FindProperty("version");
            materialsProp               = serializedObject.FindProperty("materials");
            meshesProp                  = serializedObject.FindProperty("meshes");
            shapesProp                  = serializedObject.FindProperty("shapes");
            dynamicBodiesProp           = serializedObject.FindProperty("dynamicBodies");
            cctBodiesProp               = serializedObject.FindProperty("cctBodies");
            characterMoveConfigsProp    = serializedObject.FindProperty("characterMoveConfigs");
            kinematicDriverConfigsProp  = serializedObject.FindProperty("kinematicDriverConfigs");
            projectileConfigsProp       = serializedObject.FindProperty("projectileConfigs");
            physicsArchetypesProp       = serializedObject.FindProperty("physicsArchetypes");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.HelpBox(
                "PhysicsAssetDatabase is the Unity-side shared physics registry for physics_asset.json. " +
                "Load imports JSON into shared ScriptableObject assets and registers them here. Save exports this database back to JSON.",
                MessageType.Info);

            DrawActions();
            DrawConfiguration();
            DrawAssetSection("Materials", materialsProp, ref showMaterials);
            DrawAssetSection("Meshes", meshesProp, ref showMeshes);
            DrawAssetSection("Shapes", shapesProp, ref showShapes);
            DrawAssetSection("Dynamic Bodies", dynamicBodiesProp, ref showDynamicBodies);
            DrawAssetSection("Cct Bodies", cctBodiesProp, ref showCctBodies);
            DrawAssetSection("Character Move Configs", characterMoveConfigsProp, ref showMoveConfigs);
            DrawAssetSection("Kinematic Driver Configs", kinematicDriverConfigsProp, ref showKinematicConfigs);
            DrawAssetSection("Projectile Configs", projectileConfigsProp, ref showProjectileConfigs);
            DrawAssetSection("Physics Archetypes", physicsArchetypesProp, ref showPhysicsArchetypes);
            DrawWarnings();

            serializedObject.ApplyModifiedProperties();
        }

        private void DrawConfiguration()
        {
            EditorGUILayout.LabelField("Configuration", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(sharedDataPathProp, new GUIContent("Manifest-Relative File"));
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextField("Database Asset Path", PhysicsAssetDatabase.DatabaseAssetPath);
                EditorGUILayout.TextField("Asset Data Root", PhysicsAssetDatabase.PhysicsDataRoot);
                EditorGUILayout.PropertyField(versionProp, new GUIContent("Version"));
            }
            EditorGUILayout.Space();
        }

        private void DrawAssetSection(string title, SerializedProperty arrayProp, ref bool foldout)
        {
            if (arrayProp == null)
                return;

            foldout = EditorGUILayout.Foldout(foldout, title, true);
            if (!foldout)
                return;

            for (int i = 0; i < arrayProp.arraySize; ++i)
                DrawAssetEntry(arrayProp, arrayProp.GetArrayElementAtIndex(i), i);

            using (new EditorGUILayout.HorizontalScope())
            {
                GUILayout.FlexibleSpace();
                if (GUILayout.Button("Add Entry", GUILayout.Width(120)))
                    arrayProp.InsertArrayElementAtIndex(arrayProp.arraySize);
            }

            EditorGUILayout.Space();
        }

        private static void DrawAssetEntry(SerializedProperty arrayProp, SerializedProperty assetProp, int index)
        {
            IAssetData assetData = assetProp.objectReferenceValue as IAssetData;
            UnityEngine.Object assetObject = assetProp.objectReferenceValue;

            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField($"Entry {index}", EditorStyles.boldLabel);
            if (GUILayout.Button("Remove", GUILayout.Width(70)))
            {
                arrayProp.DeleteArrayElementAtIndex(index);
                EditorGUILayout.EndHorizontal();
                EditorGUILayout.EndVertical();
                return;
            }
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.PropertyField(assetProp, new GUIContent("Asset"));
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextField("Asset Name", assetData != null ? assetData.AssetName : string.Empty);
                EditorGUILayout.TextField("Type", assetObject != null ? assetObject.GetType().Name : string.Empty);
            }

            if (assetProp.objectReferenceValue is PhysicsArchetypeData physicsArchetype)
            {
                using (new EditorGUI.DisabledScope(true))
                {
                    EditorGUILayout.TextField("Actor Type", physicsArchetype.ActorType.ToString());
                    EditorGUILayout.TextField("Body Type", physicsArchetype.BodyType.ToString());
                    EditorGUILayout.TextField("Motion Type", physicsArchetype.MotionType.ToString());
                }
            }

            EditorGUILayout.EndVertical();
        }

        private void DrawActions()
        {
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Load Physics Asset"))
                    InvokeMutation("Load Physics Asset", LoadAsset);

                if (GUILayout.Button("Save Physics Asset"))
                    InvokeMutation("Save Physics Asset", SaveAsset);
            }

            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Validate"))
                    InvokeMutation("Validate Physics Asset", ValidateAsset);
            }

            EditorGUILayout.Space();
        }

        private void DrawWarnings()
        {
            PhysicsAssetDatabase asset = (PhysicsAssetDatabase)target;
            if (!asset.TryValidateEntries(out var entryErrors))
            {
                EditorGUILayout.LabelField("Entry Warnings", EditorStyles.boldLabel);
                for (int i = 0; i < entryErrors.Count; ++i)
                    EditorGUILayout.HelpBox(entryErrors[i], MessageType.Warning);
            }

            if (!asset.ValidateForExport(out string message) && !string.IsNullOrWhiteSpace(message))
            {
                EditorGUILayout.LabelField("Warnings", EditorStyles.boldLabel);
                string[] lines = message.Split('\n');
                for (int i = 0; i < lines.Length; ++i)
                {
                    string line = lines[i].Trim();
                    if (string.IsNullOrWhiteSpace(line) || line == "Physics asset export failed.")
                        continue;

                    if (line.StartsWith("- "))
                        line = line.Substring(2);

                    EditorGUILayout.HelpBox(line, MessageType.Warning);
                }
            }
        }

        private void InvokeMutation(string undoLabel, MutationFunc mutation)
        {
            serializedObject.ApplyModifiedProperties();

            PhysicsAssetDatabase asset = (PhysicsAssetDatabase)target;
            Undo.RecordObject(asset, undoLabel);
            bool ok = mutation(asset, out string message);
            if (ok)
            {
                EditorUtility.SetDirty(asset);
                if (!string.IsNullOrWhiteSpace(message))
                    Debug.Log(message);
            }
            else if (!string.IsNullOrWhiteSpace(message))
            {
                Debug.LogError(message);
            }

            serializedObject.Update();
        }

        private static bool LoadAsset(PhysicsAssetDatabase asset, out string message)
        {
            bool ok = asset.LoadFromJson(out message);
            if (ok)
                message = $"Loaded physics asset from {asset.ResolvedSharedDataPath}";
            return ok;
        }

        private static bool SaveAsset(PhysicsAssetDatabase asset, out string message)
        {
            bool ok = asset.SaveToJson(out string outputPath, out message);
            if (ok)
                message = $"Saved physics asset to {outputPath}";
            return ok;
        }

        private static bool ValidateAsset(PhysicsAssetDatabase asset, out string message)
        {
            bool ok = asset.ValidateForExport(out message);
            if (ok)
                message = "Physics asset validation passed.";
            return ok;
        }

        private delegate bool MutationFunc(PhysicsAssetDatabase asset, out string message);
    }
} // namespace JamUnity.Editor.Physics
