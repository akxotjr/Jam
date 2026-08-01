using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

using JamUnity.Actor.Runtime;
using JamUnity.Authoring.Actor;
using JamUnity.World.Runtime;

namespace JamUnity.Editor.Actor.Runtime
{
    [CustomEditor(typeof(ActorLevelAuthoring))]
    public sealed class ActorLevelAuthoringEditor : UnityEditor.Editor
    {
        private SerializedProperty exportEnabledProp;
        private SerializedProperty actorIdProp;
        private SerializedProperty actorArchetypeProp;

        private void OnEnable()
        {
            exportEnabledProp = serializedObject.FindProperty("exportEnabled");
            actorIdProp = serializedObject.FindProperty("actorId");
            actorArchetypeProp = serializedObject.FindProperty("actorArchetype");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            var authoring = (ActorLevelAuthoring)target;

			EditorGUILayout.PropertyField(exportEnabledProp);
			EditorGUILayout.PropertyField(actorIdProp);
			if (GUILayout.Button("Assign New Level Actor Id"))
			{
				AssignNewActorId(authoring);
				serializedObject.Update();
			}
			EditorGUILayout.PropertyField(actorArchetypeProp, new GUIContent("Actor Archetype"));

            DrawWarnings(authoring);

            serializedObject.ApplyModifiedProperties();
        }

		private static void DrawWarnings(ActorLevelAuthoring authoring)
        {
            if (!authoring.ExportEnabled)
            {
                EditorGUILayout.HelpBox("Export가 꺼져 있으면 world level json에 포함되지 않습니다.", MessageType.Info);
                return;
            }

			if (authoring.ActorArchetype == null)
            {
                EditorGUILayout.HelpBox("level-spawn actorArchetype이 비어 있습니다.", MessageType.Warning);
                return;
			}

			if (authoring.ActorId == 0)
				EditorGUILayout.HelpBox("actorId가 아직 없습니다. Export 시 영속 ID가 할당됩니다.", MessageType.Warning);
			else if (!ActorLevelAuthoring.IsCanonicalActorId(authoring.ActorId))
				EditorGUILayout.HelpBox("actorId는 canonical generation-1 ActorId여야 합니다.", MessageType.Error);

            string archetypeName = authoring.ActorArchetype.ArchetypeName?.Trim() ?? string.Empty;
            if (string.IsNullOrWhiteSpace(archetypeName))
            {
                EditorGUILayout.HelpBox("actorArchetype의 AssetName이 비어 있습니다.", MessageType.Warning);
                return;
            }

            WorldRoot worldRoot = authoring.GetComponentInParent<WorldRoot>(true);
            if (worldRoot == null)
            {
                EditorGUILayout.HelpBox("ActorLevelAuthoring must be under a WorldRoot.", MessageType.Warning);
                return;
            }

            ActorPresentationCatalog presentationCatalog = ActorPresentationCatalogEditor.FindCatalog();
            if (presentationCatalog == null || !presentationCatalog.TryGetActorPrefab(archetypeName, out GameObject actorPrefab))
            {
                EditorGUILayout.HelpBox($"Global ActorPresentationCatalog has no prefab binding for '{archetypeName}'.", MessageType.Warning);
                return;
            }

            if (actorPrefab.GetComponent<ActorRootMarker>() == null)
                EditorGUILayout.HelpBox("Presentation prefab root requires ActorRootMarker.", MessageType.Warning);

			if (actorPrefab.GetComponentInChildren<ActorPhysicalMarker>(true) == null)
				EditorGUILayout.HelpBox("Level actor presentation prefab requires a Physical child.", MessageType.Warning);
		}

		private static void AssignNewActorId(ActorLevelAuthoring authoring)
		{
			WorldRoot worldRoot = authoring.GetComponentInParent<WorldRoot>(true);
			if (worldRoot == null)
				return;

			var used = new System.Collections.Generic.HashSet<uint>();
			foreach (ActorLevelAuthoring other in worldRoot.GetComponentsInChildren<ActorLevelAuthoring>(true))
			{
				if (other != authoring && ActorLevelAuthoring.IsCanonicalActorId(other.ActorId))
					used.Add(other.ActorId);
			}

			uint nextSlot = 1;
			while (nextSlot <= ActorLevelAuthoring.MaxAuthoredSlot
				&& used.Contains(ActorLevelAuthoring.MakeInitialActorId(nextSlot)))
				++nextSlot;
			if (nextSlot == 0 || nextSlot > ActorLevelAuthoring.MaxAuthoredSlot)
				return;

			Undo.RecordObject(authoring, "Assign level actor id");
			authoring.SetActorId(ActorLevelAuthoring.MakeInitialActorId(nextSlot));
			EditorUtility.SetDirty(authoring);
			EditorSceneManager.MarkSceneDirty(authoring.gameObject.scene);
		}
    }
} // namespace JamUnity.Editor.Actor.Runtime
