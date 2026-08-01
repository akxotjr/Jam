using System.IO;
using UnityEditor;
using UnityEngine;

using JamUnity.Core.Data;
using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Physics
{
    internal static class PhysicsDataEditorUtil
    {
        public const string PhysicsDataRoot   = PhysicsAssetDatabase.PhysicsDataRoot;
        public const string MaterialsFolder   = PhysicsDataRoot + "/Materials";
        public const string MeshesFolder      = PhysicsDataRoot + "/Meshes";
        public const string ShapesFolder      = PhysicsDataRoot + "/Shapes";
        public const string BodiesFolder      = PhysicsDataRoot + "/Bodies";
        public const string MoveConfigsFolder = PhysicsDataRoot + "/MoveConfigs";
        public const string BehaviorsFolder   = PhysicsDataRoot + "/Behaviors";
        public const string PhysicsArchetypesFolder = PhysicsDataRoot + "/Archetypes";

        public static void DrawObjectFieldWithCreate<T>(SerializedProperty property, string folderPath, string defaultName) where T : ScriptableObject
        {
            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.PropertyField(property);
            if (property.objectReferenceValue == null)
            {
                if (GUILayout.Button("New", GUILayout.Width(56f)))
                {
                    T asset = CreateDataAsset<T>(folderPath, defaultName);
                    if (asset != null)
                        property.objectReferenceValue = asset;
                }
            }
            EditorGUILayout.EndHorizontal();
        }

        public static void DrawCreateAndAddButton<T>(SerializedProperty arrayProperty, string folderPath, string defaultName) where T : ScriptableObject
        {
            EditorGUILayout.BeginHorizontal();
            GUILayout.FlexibleSpace();
            if (GUILayout.Button("Create And Add", GUILayout.Width(120f)))
            {
                T asset = CreateDataAsset<T>(folderPath, defaultName);
                if (asset != null)
                {
                    int index = arrayProperty.arraySize;
                    arrayProperty.InsertArrayElementAtIndex(index);
                    arrayProperty.GetArrayElementAtIndex(index).objectReferenceValue = asset;
                }
            }
            EditorGUILayout.EndHorizontal();
        }

        public static T CreateDataAsset<T>(string folderPath, string defaultName) where T : ScriptableObject
        {
            AssetEditorUtil.EnsureFolderHierarchy(folderPath);

            var asset = ScriptableObject.CreateInstance<T>();
            string safeName = SanitizeFileName(defaultName);
            string assetPath = AssetDatabase.GenerateUniqueAssetPath($"{folderPath}/{safeName}.asset");
            AssetDatabase.CreateAsset(asset, assetPath);
            if (asset is IAssetData namedAsset)
                namedAsset.AssetName = safeName;

            TryRegisterWithDefaultDatabase(asset);
            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();
            EditorGUIUtility.PingObject(asset);
            Selection.activeObject = asset;
            return asset;
        }

        public static T LoadOrRepairNamedAsset<T>(string folderPath, string assetName) where T : ScriptableObject
        {
            string safeName = SanitizeFileName(assetName);
            string assetPath = $"{folderPath}/{safeName}.asset";

            if (!File.Exists(assetPath))
                return null;

            T asset = AssetDatabase.LoadAssetAtPath<T>(assetPath);
            if (asset != null)
                return asset;

            MonoScript monoScript = FindMonoScript<T>();
            if (monoScript == null)
                return null;

            string scriptPath = AssetDatabase.GetAssetPath(monoScript);
            string scriptGuid = AssetDatabase.AssetPathToGUID(scriptPath);
            if (string.IsNullOrWhiteSpace(scriptGuid))
                return null;

            string text = File.ReadAllText(assetPath);
            if (!string.IsNullOrWhiteSpace(text) && text.Contains("m_Script: {fileID: 0}"))
            {
                text = text.Replace(
                    "m_Script: {fileID: 0}",
                    $"m_Script: {{fileID: 11500000, guid: {scriptGuid}, type: 3}}");
                File.WriteAllText(assetPath, text);
                AssetDatabase.ImportAsset(assetPath, ImportAssetOptions.ForceUpdate);
                asset = AssetDatabase.LoadAssetAtPath<T>(assetPath);
                if (asset != null)
                    return asset;
            }

            File.Delete(assetPath);
            AssetDatabase.Refresh();

            asset = ScriptableObject.CreateInstance<T>();
            AssetDatabase.CreateAsset(asset, assetPath);
            if (asset is IAssetData namedAsset)
                namedAsset.AssetName = safeName;
            return asset;
        }

        public static string SanitizeFileName(string value)
        {
            return AssetEditorUtil.SanitizeFileName(string.IsNullOrWhiteSpace(value) ? "PhysicsData" : value);
        }

        private static MonoScript FindMonoScript<T>() where T : ScriptableObject
        {
            string[] guids = AssetDatabase.FindAssets($"{typeof(T).Name} t:MonoScript", new[] { "Assets/Scripts" });
            for (int i = 0; i < guids.Length; ++i)
            {
                string assetPath = AssetDatabase.GUIDToAssetPath(guids[i]);
                MonoScript script = AssetDatabase.LoadAssetAtPath<MonoScript>(assetPath);
                if (script != null && script.GetClass() == typeof(T))
                    return script;
            }

            return null;
        }

        private static void TryRegisterWithDefaultDatabase(ScriptableObject asset)
        {
            string[] guids = AssetDatabase.FindAssets("t:PhysicsAssetDatabase", new[] { PhysicsDataRoot });
            if (guids.Length != 1)
                return;

            string assetPath = AssetDatabase.GUIDToAssetPath(guids[0]);
            PhysicsAssetDatabase database = AssetDatabase.LoadAssetAtPath<PhysicsAssetDatabase>(assetPath);
            if (database == null)
                return;

            database.RegisterSharedAsset(asset);
            EditorUtility.SetDirty(database);
        }
    }

    [CustomPropertyDrawer(typeof(SimulationFilterData))]
    internal sealed class SimulationFilterDataDrawer : PropertyDrawer
    {
        private const float Spacing = 2f;
        private const int ExpandedLineCount = 10;

        public override float GetPropertyHeight(SerializedProperty property, GUIContent label)
        {
            int lineCount = property.isExpanded ? ExpandedLineCount : 1;
            return lineCount * EditorGUIUtility.singleLineHeight + (lineCount - 1) * Spacing;
        }

        public override void OnGUI(Rect position, SerializedProperty property, GUIContent label)
        {
            EditorGUI.BeginProperty(position, label, property);

            Rect line = NextLine(ref position);
            property.isExpanded = EditorGUI.Foldout(line, property.isExpanded, label, true);
            if (!property.isExpanded)
            {
                EditorGUI.EndProperty();
                return;
            }

            ++EditorGUI.indentLevel;
            DrawLabel(ref position, "JamPx Policy");
            DrawProperty(ref position, property, "category", "Category");
            DrawProperty(ref position, property, "mask", "Collision Mask");
            DrawProperty(ref position, property, "userFlags", "Contact Notifications");
            DrawLabel(ref position, "Custom / Unknown Bits");
            DrawProperty(ref position, property, "customCategoryBits", "Category Bits");
            DrawProperty(ref position, property, "customMaskBits", "Mask Bits");
            DrawProperty(ref position, property, "customUserFlagBits", "User Flag Bits");
            DrawProperty(ref position, property, "customWord3", "Word 3");
            --EditorGUI.indentLevel;

            EditorGUI.EndProperty();
        }

        private static Rect NextLine(ref Rect position)
        {
            Rect line = new Rect(position.x, position.y, position.width, EditorGUIUtility.singleLineHeight);
            position.y += EditorGUIUtility.singleLineHeight + Spacing;
            return line;
        }

        private static void DrawLabel(ref Rect position, string label)
        {
            EditorGUI.LabelField(NextLine(ref position), label, EditorStyles.miniBoldLabel);
        }

        private static void DrawProperty(
            ref Rect position,
            SerializedProperty parent,
            string propertyName,
            string label)
        {
            EditorGUI.PropertyField(
                NextLine(ref position),
                parent.FindPropertyRelative(propertyName),
                new GUIContent(label));
        }
    }

    [CustomPropertyDrawer(typeof(ShapeQueryFilterData))]
    internal sealed class ShapeQueryFilterDataDrawer : PropertyDrawer
    {
        private const float Spacing = 2f;
        private const int ExpandedLineCount = 13;

        public override float GetPropertyHeight(SerializedProperty property, GUIContent label)
        {
            int lineCount = property.isExpanded ? ExpandedLineCount : 1;
            return lineCount * EditorGUIUtility.singleLineHeight + (lineCount - 1) * Spacing;
        }

        public override void OnGUI(Rect position, SerializedProperty property, GUIContent label)
        {
            EditorGUI.BeginProperty(position, label, property);

            Rect line = NextLine(ref position);
            property.isExpanded = EditorGUI.Foldout(line, property.isExpanded, label, true);
            if (!property.isExpanded)
            {
                EditorGUI.EndProperty();
                return;
            }

            ++EditorGUI.indentLevel;
            DrawLabel(ref position, "JamPx Policy");
            DrawProperty(ref position, property, "category", "Category");
            DrawUInt(ref position, property, "channel", "Channel", byte.MaxValue);
            DrawUInt(ref position, property, "sublayer", "Sublayer", byte.MaxValue);
            DrawUInt(ref position, property, "tag", "Tag", ushort.MaxValue);
            DrawUInt(ref position, property, "team", "Team", ushort.MaxValue);
            DrawUInt(ref position, property, "part", "Part", byte.MaxValue);
            DrawUInt(ref position, property, "role", "Role", byte.MaxValue);
            DrawProperty(ref position, property, "flags", "Shape Flags");
            DrawLabel(ref position, "Custom / Unknown Bits");
            DrawProperty(ref position, property, "customCategoryBits", "Category Bits");
            DrawProperty(ref position, property, "customFlagBits", "Shape Flag Bits");
            --EditorGUI.indentLevel;

            EditorGUI.EndProperty();
        }

        private static Rect NextLine(ref Rect position)
        {
            Rect line = new Rect(position.x, position.y, position.width, EditorGUIUtility.singleLineHeight);
            position.y += EditorGUIUtility.singleLineHeight + Spacing;
            return line;
        }

        private static void DrawLabel(ref Rect position, string label)
        {
            EditorGUI.LabelField(NextLine(ref position), label, EditorStyles.miniBoldLabel);
        }

        private static void DrawProperty(
            ref Rect position,
            SerializedProperty parent,
            string propertyName,
            string label)
        {
            EditorGUI.PropertyField(
                NextLine(ref position),
                parent.FindPropertyRelative(propertyName),
                new GUIContent(label));
        }

        private static void DrawUInt(
            ref Rect position,
            SerializedProperty parent,
            string propertyName,
            string label,
            uint maximum)
        {
            SerializedProperty value = parent.FindPropertyRelative(propertyName);
            long edited = EditorGUI.LongField(NextLine(ref position), label, value.longValue);
            value.longValue = System.Math.Min(System.Math.Max(edited, 0L), maximum);
        }
    }
} // namespace JamUnity.Editor.Physics
