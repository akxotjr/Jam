using UnityEngine;

using JamUnity.Runtime.Client;
using JamUnity.World.Runtime;

namespace JamUnity.Client.Runtime
{
    public class CameraFollow : MonoBehaviour
    {
        [SerializeField] private WorldPresenter presenter;

        private CameraProfile profile;
        private Camera targetCamera;

        public void SetProfile(CameraProfile nextProfile)
        {
            profile = nextProfile;
            targetCamera ??= GetComponent<Camera>();

            if (targetCamera == null || profile == null)
                return;

            targetCamera.orthographic = profile.Orthographic;
            targetCamera.fieldOfView = profile.FieldOfView;
            targetCamera.orthographicSize = profile.OrthographicSize;
        }
    
        void LateUpdate()
        {
            if (profile == null)
                return;

            if (presenter == null)
                presenter = ResolvePresenter();
    
            if (presenter == null)
                return;
    
            Transform target = presenter.GetLocalActorTransform();
            if (target == null)
                return;
    
            Vector3 desiredPosition = target.position + profile.FollowOffset;
            float blend = 1.0f - Mathf.Exp(-profile.SmoothSpeed * Time.deltaTime);
            transform.position = Vector3.Lerp(transform.position, desiredPosition, blend);
            transform.LookAt(target.position + profile.LookAtOffset);
        }
    
        private static WorldPresenter ResolvePresenter()
        {
            WorldRoot[] roots = FindObjectsByType<WorldRoot>(FindObjectsInactive.Exclude, FindObjectsSortMode.None);
            WorldPresenter candidate = null;
    
            for (int i = 0; i < roots.Length; ++i)
            {
                WorldRoot root = roots[i];
                if (root == null || !root.isActiveAndEnabled || !root.gameObject.scene.isLoaded || root.WorldPresenter == null)
                    continue;
    
                if (candidate == null)
                {
                    candidate = root.WorldPresenter;
                    continue;
                }
    
                if (root.gameObject.scene.path.Replace('\\', '/').Contains("/Scenes/World", System.StringComparison.OrdinalIgnoreCase))
                    candidate = root.WorldPresenter;
            }
    
            return candidate;
        }
    }
    
} // namespace JamUnity.Client.Runtime
