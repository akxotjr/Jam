using System.Collections.Generic;
using JamUnity.Client.Runtime;
using JamUnity.World.Runtime;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace JamUnity.UI
{
    public sealed class AoiMinimap : MonoBehaviour
    {
        private const float MapInset = 12.0f;
        private const float HeaderHeight = 32.0f;
        private const float MapSize = 320.0f;
        private static readonly Color32 PanelColor = new(14, 22, 32, 232);
        private static readonly Color32 MapColor = new(27, 39, 53, 255);
        private static readonly Color32 EnterColor = new(75, 220, 235, 255);
        private static readonly Color32 RetainColor = new(255, 173, 65, 255);
        private static readonly Color32 RemoteColor = new(217, 231, 250, 255);
        private static readonly Color32 LocalColor = new(255, 72, 85, 255);

        private readonly List<Vector3> remotePositions = new();
        private readonly List<RectTransform> remoteMarkers = new();
        private TMP_Text title;
        private RectTransform map;
        private RectTransform[] enterBoundary;
        private RectTransform[] retainBoundary;
        private RectTransform localMarker;

        public static AoiMinimap Create(Canvas canvas)
        {
            var panel = new GameObject("AOI Minimap", typeof(RectTransform), typeof(AoiMinimap));
            panel.SetActive(false);
            var minimap = panel.GetComponent<AoiMinimap>();
            RectTransform rect = panel.GetComponent<RectTransform>();
            rect.SetParent(canvas.transform, false);
            rect.anchorMin = rect.anchorMax = rect.pivot = new Vector2(1.0f, 0.0f);
            rect.anchoredPosition = new Vector2(-20.0f, 20.0f);
            rect.sizeDelta = new Vector2(MapSize + 2.0f * MapInset, MapSize + HeaderHeight + MapInset);
            AddImage(panel, PanelColor);

            minimap.map = CreateImage("Map", rect, MapColor);
            minimap.map.anchoredPosition = Vector2.down * ((HeaderHeight - MapInset) * 0.5f);
            minimap.map.gameObject.AddComponent<RectMask2D>();
            minimap.enterBoundary = CreateBoundary(minimap.map, "Enter", EnterColor);
            minimap.retainBoundary = CreateBoundary(minimap.map, "Retain", RetainColor);
            minimap.localMarker = CreateImage("Local Player", minimap.map, LocalColor);
            minimap.localMarker.sizeDelta = Vector2.one * 8.0f;

            var titleObject = new GameObject("Field Name", typeof(RectTransform), typeof(TextMeshProUGUI));
            minimap.title = titleObject.GetComponent<TextMeshProUGUI>();
            minimap.title.raycastTarget = false;
            minimap.title.richText = false;
            minimap.title.fontSize = 19.0f;
            minimap.title.alignment = TextAlignmentOptions.MidlineLeft;
            minimap.title.overflowMode = TextOverflowModes.Ellipsis;
            RectTransform titleRect = minimap.title.rectTransform;
            titleRect.SetParent(rect, false);
            titleRect.anchorMin = new Vector2(0.0f, 1.0f);
            titleRect.anchorMax = Vector2.one;
            titleRect.offsetMin = new Vector2(MapInset, -HeaderHeight);
            titleRect.offsetMax = new Vector2(-MapInset, 0.0f);
            minimap.SetWorldName("World");
            return minimap;
        }

        public void SetWorldName(string worldName)
        {
            title.text = worldName;
        }

        public void Present(WorldPresenter presenter, CameraFollow cameraFollow, bool inWorld)
        {
            Transform localActor = inWorld && presenter != null ? presenter.GetLocalActorTransform() : null;
            Bounds floorBounds = default;
            bool visible = localActor != null && cameraFollow != null && presenter.TryGetMinimapBounds(out floorBounds);
            if (gameObject.activeSelf != visible)
                gameObject.SetActive(visible);
            if (!visible)
                return;

            Vector2 floorSize = new Vector2(floorBounds.size.x, floorBounds.size.z);
            float scale = MapSize / Mathf.Max(floorSize.x, floorSize.y);
            map.sizeDelta = floorSize * scale;
            Vector3 mapOrigin = floorBounds.center;
            Vector2 localPoint = Project(localActor.position, mapOrigin, scale);
            localMarker.anchoredPosition = localPoint;

            float enterHalfExtent = cameraFollow.AoiHalfExtent;
            float retainHalfExtent = enterHalfExtent + cameraFollow.AoiHysteresis;
            SetBoundary(enterBoundary, localPoint, enterHalfExtent * scale);
            SetBoundary(retainBoundary, localPoint, retainHalfExtent * scale);

            presenter.CopyRemoteActorPositions(remotePositions);
            const float markerSize = 5.0f;
            Vector2 mapHalfSize = map.sizeDelta * 0.5f;
            int visibleCount = 0;
            foreach (Vector3 position in remotePositions)
            {
                Vector2 point = Project(position, mapOrigin, scale);
                if (Mathf.Abs(point.x) > mapHalfSize.x || Mathf.Abs(point.y) > mapHalfSize.y)
                    continue;

                if (visibleCount == remoteMarkers.Count)
                {
                    RectTransform marker = CreateImage("Remote Actor", map, RemoteColor);
                    marker.sizeDelta = Vector2.one * markerSize;
                    marker.SetSiblingIndex(localMarker.GetSiblingIndex());
                    remoteMarkers.Add(marker);
                }

                RectTransform visibleMarker = remoteMarkers[visibleCount++];
                visibleMarker.anchoredPosition = point;
                visibleMarker.gameObject.SetActive(true);
            }

            for (int i = visibleCount; i < remoteMarkers.Count; ++i)
                remoteMarkers[i].gameObject.SetActive(false);
        }

        private static Vector2 Project(Vector3 position, Vector3 mapOrigin, float scale)
        {
            Vector3 offset = position - mapOrigin;
            return new Vector2(offset.x, offset.z) * scale;
        }

        private static RectTransform CreateImage(string name, RectTransform parent, Color32 color)
        {
            var child = new GameObject(name, typeof(RectTransform));
            var rect = child.GetComponent<RectTransform>();
            rect.SetParent(parent, false);
            rect.anchorMin = rect.anchorMax = new Vector2(0.5f, 0.5f);
            AddImage(child, color);
            return rect;
        }

        private static void AddImage(GameObject target, Color32 color)
        {
            var image = target.AddComponent<Image>();
            image.color = color;
            image.raycastTarget = false;
        }

        private static RectTransform[] CreateBoundary(RectTransform parent, string name, Color32 color)
        {
            return new[]
            {
                CreateImage(name + " Top", parent, color),
                CreateImage(name + " Bottom", parent, color),
                CreateImage(name + " Left", parent, color),
                CreateImage(name + " Right", parent, color),
            };
        }

        private static void SetBoundary(RectTransform[] sides, Vector2 center, float extent)
        {
            const float width = 2.0f;
            float size = extent * 2.0f;
            sides[0].anchoredPosition = center + new Vector2(0.0f, extent);
            sides[1].anchoredPosition = center + new Vector2(0.0f, -extent);
            sides[2].anchoredPosition = center + new Vector2(-extent, 0.0f);
            sides[3].anchoredPosition = center + new Vector2(extent, 0.0f);
            sides[0].sizeDelta = sides[1].sizeDelta = new Vector2(size + width, width);
            sides[2].sizeDelta = sides[3].sizeDelta = new Vector2(width, size);
        }
    }
}
