using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Newtonsoft.Json;
using UnityEditor;
using UnityEngine;

using JamUnity.Actor.Runtime;
using JamUnity.World.Runtime;
using JamUnity.Authoring.Actor;

using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Editor.Actor
{
    public static class ActorLevelExporter
    {
        private static readonly JsonSerializerSettings JsonSettings = new()
        {
            Formatting = Formatting.Indented,
            NullValueHandling = NullValueHandling.Ignore,
        };

        [MenuItem("Tools/JamUnity/Export/Selected World Level")]
        public static void ExportSelectedWorldLevel()
        {
            if (!TryExportSelectedWorldLevel(out string outputPath, out string errorMessage))
            {
                Debug.LogError(errorMessage);
                return;
            }
        }

        [MenuItem("Tools/JamUnity/Export/Selected World Level", true)]
        private static bool ValidateExportSelectedWorldLevel()
        {
            return Selection.activeGameObject != null
                && Selection.activeGameObject.GetComponentInParent<WorldRoot>(true) != null;
        }

        [MenuItem("Tools/JamUnity/Validate/Selected World Level")]
        public static void ValidateSelectedWorldLevel()
        {
            if (TryValidateSelectedWorldLevel(out string errorMessage))
            {
                Debug.Log("Level validation passed.");
                return;
            }

            Debug.LogError(errorMessage);
        }

        [MenuItem("Tools/JamUnity/Validate/Selected World Level", true)]
        private static bool ValidateValidateSelectedWorldLevel()
        {
            return Selection.activeGameObject != null
                && Selection.activeGameObject.GetComponentInParent<WorldRoot>(true) != null;
        }

        public static string GetDefaultOutputPath(WorldRoot worldRoot)
        {
            string worldName = worldRoot != null ? worldRoot.name?.Trim() ?? string.Empty : string.Empty;
            return string.IsNullOrWhiteSpace(worldName)
                ? string.Empty
                : JamUnity.Core.Util.Path.ResolveSharedDataPath($"Levels/{worldName}.actor_level.json");
        }

        public static bool TryExportSelectedWorldLevel(out string outputPath, out string errorMessage)
        {
            outputPath = string.Empty;
            errorMessage = string.Empty;

            if (!TryResolveSelectedWorldRoot(out WorldRoot worldRoot, out errorMessage))
                return false;

            if (!TryCollectWorldEntries(worldRoot, out List<LevelEntry> entries, out List<string> errors))
            {
                errorMessage = FormatValidationMessage("Level export failed.", errors);
                return false;
            }

            outputPath = GetDefaultOutputPath(worldRoot);
            if (string.IsNullOrWhiteSpace(outputPath))
            {
                errorMessage = $"WorldRoot '{worldRoot.name}' does not produce a valid manifest-relative actor level path.";
                return false;
            }

            Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
            File.WriteAllText(outputPath, BuildJson(worldRoot.name, entries));
            AssetDatabase.SaveAssets();
            Debug.Log($"Exported {entries.Count} level instances to {outputPath}");
            return true;
        }

        public static bool TryValidateSelectedWorldLevel(out string errorMessage)
        {
            errorMessage = string.Empty;

            if (!TryResolveSelectedWorldRoot(out WorldRoot worldRoot, out errorMessage))
                return false;

            if (TryCollectWorldEntries(worldRoot, out List<LevelEntry> _, out List<string> errors))
                return true;

            errorMessage = FormatValidationMessage("Level validation failed.", errors);
            return false;
        }

        private static bool TryResolveSelectedWorldRoot(out WorldRoot worldRoot, out string errorMessage)
        {
            worldRoot = null;
            errorMessage = string.Empty;

            GameObject selected = Selection.activeGameObject;
            if (selected != null)
                worldRoot = selected.GetComponentInParent<WorldRoot>(true);

            if (worldRoot != null)
                return true;

            errorMessage = "Select a WorldRoot or one of its children before exporting a level.";
            return false;
        }

        private static bool TryCollectWorldEntries(WorldRoot worldRoot, out List<LevelEntry> entries, out List<string> errors)
        {
            entries = new List<LevelEntry>();
            errors = new List<string>();

            ActorPresentationCatalog presentationCatalog = ActorPresentationCatalogEditor.FindCatalog();
            if (presentationCatalog == null)
            {
                errors.Add("A single global ActorPresentationCatalog is required.");
                return false;
            }

            var authorings = new List<ActorLevelAuthoring>();
            for (int i = 0; i < worldRoot.transform.childCount; ++i)
                CollectAuthorings(worldRoot.transform.GetChild(i), authorings);

            authorings = authorings
                .Where(static x => x != null && x.ExportEnabled)
                .OrderBy(x => GetHierarchyPath(x, worldRoot.transform), StringComparer.Ordinal)
                .ToList();

            if (authorings.Count == 0)
            {
                errors.Add($"No enabled ActorLevelAuthoring components found under WorldRoot '{worldRoot.name}'.");
                return false;
            }

			var usedActorIds = new Dictionary<uint, string>();
			foreach (ActorLevelAuthoring authoring in authorings)
			{
				uint actorId = ActorLevelAuthoring.NormalizeLegacyActorId(authoring.ActorId);
				if (actorId == 0)
					continue;

				string objectPath = GetHierarchyPath(authoring, worldRoot.transform);
				if (!ActorLevelAuthoring.IsCanonicalActorId(actorId))
				{
					errors.Add($"[{objectPath}] actorId {actorId} is not a canonical generation-1 ActorId.");
					continue;
				}

				if (usedActorIds.TryGetValue(actorId, out string existing))
					errors.Add($"[{objectPath}] duplicate actorId {actorId} already used by {existing}.");
				else
					usedActorIds.Add(actorId, objectPath);
			}

			uint nextAutoSlot = 1;

            foreach (ActorLevelAuthoring authoring in authorings)
            {
                string objectPath = GetHierarchyPath(authoring, worldRoot.transform);
                ActorArchetypeData actorArchetype = authoring.ActorArchetype;
                if (actorArchetype == null)
                {
                    errors.Add($"[{objectPath}] actorArchetype is null.");
                    continue;
                }

                string actorArchetypeName = actorArchetype.ArchetypeName?.Trim() ?? string.Empty;
                if (string.IsNullOrWhiteSpace(actorArchetypeName))
                {
                    errors.Add($"[{objectPath}] actor archetype name is empty.");
                    continue;
                }

                if (!presentationCatalog.TryGetActorPrefab(actorArchetypeName, out GameObject actorPrefab))
                {
                    errors.Add($"[{objectPath}] ActorPresentationCatalog has no prefab binding for '{actorArchetypeName}'.");
                    continue;
                }

                if (actorPrefab.GetComponent<ActorRootMarker>() == null)
                {
                    errors.Add($"[{objectPath}] presentation prefab for '{actorArchetypeName}' is missing ActorRootMarker.");
                    continue;
                }

                if (actorPrefab.GetComponentInChildren<ActorPhysicalMarker>(true) == null)
                {
                    errors.Add($"[{objectPath}] presentation prefab for '{actorArchetypeName}' must contain a Physical child.");
                    continue;
                }

				uint actorId = ActorLevelAuthoring.NormalizeLegacyActorId(authoring.ActorId);
				if (actorId == 0)
				{
					while (nextAutoSlot <= ActorLevelAuthoring.MaxAuthoredSlot
						&& usedActorIds.ContainsKey(ActorLevelAuthoring.MakeInitialActorId(nextAutoSlot)))
						++nextAutoSlot;
					if (nextAutoSlot == 0 || nextAutoSlot > ActorLevelAuthoring.MaxAuthoredSlot)
					{
						errors.Add($"[{objectPath}] no available actorId remains.");
						continue;
					}

					actorId = ActorLevelAuthoring.MakeInitialActorId(nextAutoSlot++);
					Undo.RecordObject(authoring, "Assign level actor id");
					authoring.SetActorId(actorId);
					EditorUtility.SetDirty(authoring);
					UnityEditor.SceneManagement.EditorSceneManager.MarkSceneDirty(authoring.gameObject.scene);
				}

				else if (actorId != authoring.ActorId)
				{
					Undo.RecordObject(authoring, "Migrate actor id");
					authoring.SetActorId(actorId);
					EditorUtility.SetDirty(authoring);
					UnityEditor.SceneManagement.EditorSceneManager.MarkSceneDirty(authoring.gameObject.scene);
				}

				if (!ActorLevelAuthoring.IsCanonicalActorId(actorId))
				{
					errors.Add($"[{objectPath}] actorId {actorId} is not a canonical generation-1 ActorId.");
					continue;
				}

				if (usedActorIds.TryGetValue(actorId, out string existing) && existing != objectPath)
				{
					errors.Add($"[{objectPath}] duplicate actorId {actorId} already used by {existing}.");
					continue;
				}

				usedActorIds[actorId] = objectPath;

                Transform t = authoring.transform;
                Transform worldTransform = worldRoot.transform;
                entries.Add(new LevelEntry
                {
                    ActorId = actorId,
                    ActorArchetypeName = actorArchetypeName,
                    Position = worldTransform.InverseTransformPoint(t.position),
                    Rotation = Quaternion.Inverse(worldTransform.rotation) * t.rotation,
                    HierarchyPath = objectPath,
                });
            }

            entries.Sort((lhs, rhs) =>
            {
                int byActorId = lhs.ActorId.CompareTo(rhs.ActorId);
                if (byActorId != 0) return byActorId;
                return string.CompareOrdinal(lhs.HierarchyPath, rhs.HierarchyPath);
            });

            return errors.Count == 0;
        }

        private static void CollectAuthorings(Transform current, List<ActorLevelAuthoring> output)
        {
            ActorLevelAuthoring authoring = current.GetComponent<ActorLevelAuthoring>();
            if (authoring != null)
                output.Add(authoring);

            for (int i = 0; i < current.childCount; ++i)
                CollectAuthorings(current.GetChild(i), output);
        }

        private static string BuildJson(string worldName, List<LevelEntry> entries)
        {
            SharedGen.ActorLevelsRootDto root = new()
            {
                version = 1,
                sceneName = worldName,
                instances = new List<SharedGen.ActorLevelInstanceDto>(entries.Count),
            };

            for (int i = 0; i < entries.Count; ++i)
            {
                LevelEntry entry = entries[i];
                root.instances.Add(new SharedGen.ActorLevelInstanceDto
                {
                    actorId = entry.ActorId,
                    actorArchetype = entry.ActorArchetypeName,
                    spawnPose = new SharedGen.SpawnPoseDto()
                    {
                        p = new List<float> { entry.Position.x, entry.Position.y, entry.Position.z },
                        q = new List<float> { entry.Rotation.x, entry.Rotation.y, entry.Rotation.z, entry.Rotation.w },
                    }
                });
            }

            return JsonConvert.SerializeObject(root, JsonSettings);
        }

        private static string GetHierarchyPath(Component component, Transform stopAt)
        {
            var stack = new Stack<string>();
            Transform current = component.transform;
            while (current != null && current != stopAt)
            {
                stack.Push(current.name);
                current = current.parent;
            }

            return string.Join("/", stack);
        }

        private static string FormatValidationMessage(string title, List<string> errors)
        {
            var sb = new System.Text.StringBuilder(title);
            for (int i = 0; i < errors.Count; ++i)
            {
                sb.AppendLine();
                sb.Append("- ").Append(errors[i]);
            }

            return sb.ToString();
        }

        private sealed class LevelEntry
        {
            public uint ActorId;
            public string ActorArchetypeName;
            public Vector3 Position;
            public Quaternion Rotation;
            public string HierarchyPath;
        }

    }
    
} // namespace JamUnity.Editor.Actor
