using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;


using JamUnity.World.Runtime;

namespace JamUnity.Editor.Client.Runtime
{
    // public static class ClientRootCreationEditor
    // {
    //     [MenuItem("GameObject/JamUnity/Create Client Root", false, 11)]
    //     public static void CreateClientRootMenu()
    //     {
    //         GameObject clientRoot = CreateClientRootInstance();
    //         Selection.activeGameObject = clientRoot;
    //     }
    //
    //     [MenuItem("GameObject/JamUnity/Create Client Root", true)]
    //     private static bool ValidateCreateClientRootMenu()
    //     {
    //         return true;
    //     }
    //
    //     public static GameObject CreateClientRootInstance(string rootName = "ClientRoot")
    //     {
    //         GameObject clientRoot = new(rootName);
    //         GameObjectUtility.SetParentAndAlign(clientRoot, Selection.activeGameObject);
    //
    //         NetworkDriver networkDriver = clientRoot.AddComponent<NetworkDriver>();
    //         WorldManager worldManager = clientRoot.AddComponent<WorldManager>();
    //         InputAdapter inputAdapter = clientRoot.AddComponent<InputAdapter>();
    //         Undo.RegisterCreatedObjectUndo(clientRoot, "Create Client Root");
    //         EditorSceneManager.MarkSceneDirty(clientRoot.scene);
    //         return clientRoot;
    //     }
    // }
} // namespace JamUnity.Editor.Client.Runtime
