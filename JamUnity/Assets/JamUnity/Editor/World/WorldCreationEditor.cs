using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using JamUnity.Actor.Runtime;
using JamUnity.Authoring.World;
using JamUnity.World.Runtime;

namespace JamUnity.Editor.World.Runtime
{
    public static class WorldCreationEditor
    {
        [MenuItem("GameObject/JamUnity/Create World", false, 12)]
        public static void CreateWorldRootMenu()
        {
            GameObject worldRoot = CreateWorldRootInstance();
            Selection.activeGameObject = worldRoot;
        }

        [MenuItem("GameObject/JamUnity/Create World Root", true)]
        private static bool ValidateCreateWorldRootMenu()
        {
            return true;
        }

        public static GameObject CreateWorldRootInstance(string worldName = "WorldRoot")
        {
            GameObject worldRoot     = new(worldName);

            GameObjectUtility.SetParentAndAlign(worldRoot, Selection.activeGameObject);

            WorldPresenter presenter = worldRoot.AddComponent<WorldPresenter>();
            ActorManager actorManager = worldRoot.AddComponent<ActorManager>();
            WorldRoot root = worldRoot.AddComponent<WorldRoot>();
            worldRoot.AddComponent<WorldAuthoring>();

            WireWorldRootReferences(presenter, actorManager, root);

            Undo.RegisterCreatedObjectUndo(worldRoot, "Create World Root");
            EditorSceneManager.MarkSceneDirty(worldRoot.scene);
            return worldRoot;
        }

        private static void WireWorldRootReferences(
            WorldPresenter presenter,
            ActorManager actorManager,
            WorldRoot root)
        {
            SerializedObject presenterSo = new(presenter);
            presenterSo.FindProperty("actorManager").objectReferenceValue = actorManager;
            presenterSo.FindProperty("worldRoot").objectReferenceValue = root;
            presenterSo.ApplyModifiedPropertiesWithoutUndo();

            SerializedObject actorManagerSo = new(actorManager);
            actorManagerSo.FindProperty("defaultParent").objectReferenceValue = root.transform;
            actorManagerSo.ApplyModifiedPropertiesWithoutUndo();

            SerializedObject rootSo = new(root);
            rootSo.FindProperty("worldPresenter").objectReferenceValue = presenter;
            rootSo.FindProperty("actorManager").objectReferenceValue = actorManager;
            rootSo.ApplyModifiedPropertiesWithoutUndo();

            EditorUtility.SetDirty(presenter);
            EditorUtility.SetDirty(actorManager);
            EditorUtility.SetDirty(root);
        }
    }
} // namespace JamUnity.Editor.World.Runtime
