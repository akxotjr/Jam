using UnityEditor;
using JamUnity.World.Runtime;

namespace JamUnity.Editor.World.Runtime
{
    [CustomEditor(typeof(WorldRoot))]
    public sealed class WorldRootEditor : UnityEditor.Editor
    {
        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.HelpBox(
                "WorldRoot is the world-local runtime root. Keep WorldPresenter and ActorManager here and place authored level actors directly below it. The client injects the global ActorPresentationCatalog at runtime.",
                MessageType.Info);

            DrawDefaultInspector();

            var worldRoot = (WorldRoot)target;
            DrawWarnings(worldRoot);

            serializedObject.ApplyModifiedProperties();
        }

        private static void DrawWarnings(WorldRoot worldRoot)
        {
            if (worldRoot.WorldPresenter == null)
                EditorGUILayout.HelpBox("WorldRoot requires WorldPresenter.", MessageType.Warning);

            if (worldRoot.ActorManager == null)
                EditorGUILayout.HelpBox("WorldRoot requires ActorManager.", MessageType.Warning);
        }
    }
} // namespace JamUnity.Editor.World.Runtime
