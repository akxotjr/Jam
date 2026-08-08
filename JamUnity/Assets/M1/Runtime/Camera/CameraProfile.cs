using UnityEngine;

namespace JamUnity.Runtime.Client
{
    public enum CameraProfileKind
    {
        TopView,
        ThirdPerson,
    }

    [CreateAssetMenu(menuName = "JamUnity/Client/Camera Profile", fileName = "CameraProfile")]
    public sealed class CameraProfile : ScriptableObject
    {
        [SerializeField] private CameraProfileKind kind = CameraProfileKind.TopView;
        [SerializeField] private bool orthographic;
        [SerializeField, Min(0.01f)] private float fieldOfView = 60.0f;
        [SerializeField, Min(0.01f)] private float orthographicSize = 5.0f;
        [SerializeField] private Vector3 followOffset = new(0.0f, 6.0f, -8.0f);
        [SerializeField] private Vector3 lookAtOffset = new(0.0f, 1.0f, 0.0f);
        [SerializeField, Min(0.0f)] private float smoothSpeed = 8.0f;
        [SerializeField, Min(0.0f)] private float mouseOrbitSensitivity = 0.15f;
        [SerializeField] private float initialOrbitYaw;
        [SerializeField] private float initialOrbitPitch;
        [SerializeField] private Vector2 orbitPitchLimits = new(-60.0f, 75.0f);
        [SerializeField, Min(1.0f)] private float orbitInputRate = 30.0f;

        public CameraProfileKind Kind => kind;
        public bool Orthographic => orthographic;
        public float FieldOfView => fieldOfView;
        public float OrthographicSize => orthographicSize;
        public Vector3 FollowOffset => followOffset;
        public Vector3 LookAtOffset => lookAtOffset;
        public float SmoothSpeed => smoothSpeed;
        public float MouseOrbitSensitivity => mouseOrbitSensitivity;
        public float InitialOrbitYaw => initialOrbitYaw;
        public float InitialOrbitPitch => initialOrbitPitch;
        public Vector2 OrbitPitchLimits => orbitPitchLimits;
        public float OrbitInputRate => orbitInputRate;
    }
}
