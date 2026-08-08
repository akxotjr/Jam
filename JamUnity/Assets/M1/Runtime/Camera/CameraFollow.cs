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
        private float orbitYaw;
        private float orbitPitch;

        public void SetProfile(CameraProfile nextProfile)
        {
            profile = nextProfile;
            targetCamera ??= GetComponent<Camera>();

            if (targetCamera == null || profile == null)
                return;

            targetCamera.orthographic = profile.Orthographic;
            targetCamera.fieldOfView = profile.FieldOfView;
            targetCamera.orthographicSize = profile.OrthographicSize;

            orbitYaw = profile.InitialOrbitYaw;
            orbitPitch = Mathf.Clamp(
                profile.InitialOrbitPitch,
                profile.OrbitPitchLimits.x,
                profile.OrbitPitchLimits.y);
        }

        public void SetOrbit(float yaw, float pitch)
        {
            orbitYaw = yaw;
            orbitPitch = pitch;
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

            Vector3 followOffset = profile.Kind == CameraProfileKind.ThirdPerson
                ? Quaternion.Euler(orbitPitch, orbitYaw, 0.0f) * profile.FollowOffset
                : profile.FollowOffset;
            transform.position = target.position + followOffset;
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
