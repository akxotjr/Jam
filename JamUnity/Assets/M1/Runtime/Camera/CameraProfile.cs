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

        public CameraProfileKind Kind => kind;
        public bool Orthographic => orthographic;
        public float FieldOfView => fieldOfView;
        public float OrthographicSize => orthographicSize;
        public Vector3 FollowOffset => followOffset;
        public Vector3 LookAtOffset => lookAtOffset;
        public float SmoothSpeed => smoothSpeed;
    }
}
