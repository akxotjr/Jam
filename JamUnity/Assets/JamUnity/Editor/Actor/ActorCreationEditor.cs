using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using PrefabUtility = UnityEditor.PrefabUtility;

using JamUnity.Actor.Runtime;
using JamUnity.Authoring.Physics;

namespace JamUnity.Editor.Actor
{
    public static class ActorCreationEditor
    {
        [MenuItem("GameObject/JamUnity/Create Actor", false, 10)]
        public static void CreateActorMenu()
        {
            GameObject actorRoot = CreateActorRootInstance();
            Selection.activeGameObject = actorRoot;
        }

        [MenuItem("GameObject/JamUnity/Create Actor", true)]
        private static bool ValidateCreateActorMenu()
        {
            return true;
        }

        public static GameObject CreateActorRootInstance(string actorName = "ActorRoot")
        {
            GameObject actorRoot = new(actorName);
            GameObject visual    = new("Visual");

            GameObjectUtility.SetParentAndAlign(actorRoot, Selection.activeGameObject);
            visual.transform.SetParent(actorRoot.transform, false);
            actorRoot.AddComponent<ActorRootMarker>();
            visual.AddComponent<ActorVisualMarker>();

            Undo.RegisterCreatedObjectUndo(actorRoot, "Create Actor");
            EditorSceneManager.MarkSceneDirty(actorRoot.scene);
            return actorRoot;
        }

        [MenuItem("GameObject/JamUnity/Add Physical Part", false, 11)]
        public static void AddPhysicalPartMenu()
        {
            GameObject actorRoot = Selection.activeGameObject;
            if (actorRoot == null)
            {
                Debug.LogError("Select an ActorRoot GameObject first.");
                return;
            }

            AddPhysicalPart(actorRoot);
            Selection.activeGameObject = actorRoot;
        }

        [MenuItem("GameObject/JamUnity/Add Physical Part", true)]
        private static bool ValidateAddPhysicalPartMenu()
        {
            return Selection.activeGameObject != null;
        }

        public static GameObject AddPhysicalPart(GameObject actorRoot)
        {
            if (actorRoot == null)
                return null;

            ActorPhysicalMarker existingMarker = actorRoot.GetComponentInChildren<ActorPhysicalMarker>(true);
            if (existingMarker != null)
                return existingMarker.gameObject;

            GameObject physical = new("Physical");
            physical.transform.SetParent(actorRoot.transform, false);
            physical.AddComponent<ActorPhysicalMarker>();
            physical.AddComponent<PhysicsArchetypeAuthoring>();
            Undo.RegisterCreatedObjectUndo(physical, "Add Physical Part");
            EditorSceneManager.MarkSceneDirty(actorRoot.scene);
            return physical;
        }

        public static void CreateActorPrefabAndLink(ActorPresentationCatalog catalog, string actorArchetypeName)
        {
            string actorName = !string.IsNullOrWhiteSpace(actorArchetypeName) ? actorArchetypeName.Trim() : "ActorRoot";
            GameObject actorRoot = CreateActorRootInstance(actorName);
            SaveActorRootAsPrefabAndLink(catalog, actorArchetypeName, actorRoot);
        }

        public static void SaveActorRootAsPrefabAndLink(ActorPresentationCatalog catalog, string actorArchetypeName, GameObject actorRoot)
        {
            if (catalog == null || !catalog.TryGetEntry(actorArchetypeName, out ActorPresentationCatalog.Entry entry))
            {
                Debug.LogError("Actor presentation binding is missing.");
                return;
            }

            if (actorRoot == null)
            {
                Debug.LogError("ActorRoot selection is null.");
                return;
            }

            string defaultName = !string.IsNullOrWhiteSpace(actorArchetypeName) ? actorArchetypeName.Trim() : actorRoot.name;
            string path = EditorUtility.SaveFilePanelInProject(
                "Save Actor Prefab",
                defaultName,
                "prefab",
                "Select actor prefab path.",
                Core.Util.Path.ActorContentAssetRoot);

            if (string.IsNullOrWhiteSpace(path))
                return;

            GameObject prefabAsset = PrefabUtility.SaveAsPrefabAssetAndConnect(actorRoot, path, InteractionMode.UserAction);
            if (prefabAsset == null)
            {
                Debug.LogError($"Failed to save actor prefab: {path}");
                return;
            }

            Undo.RecordObject(catalog, "Link Actor Presentation Prefab");
            entry.ActorPrefab = prefabAsset;
            EditorUtility.SetDirty(catalog);
            AssetDatabase.SaveAssets();

            Selection.activeObject = catalog;
            Debug.Log($"Saved actor prefab and linked presentation binding: {path}");
        }
    }
    
} // namespace JamUnity.Editor.Actor
