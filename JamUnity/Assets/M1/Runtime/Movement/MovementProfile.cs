using UnityEngine;

namespace JamUnity.Runtime.Client
{
    public enum MovementProfileKind
    {
        Directional,
        PointAndClick,
    }

    [CreateAssetMenu(menuName = "JamUnity/Client/Movement Profile", fileName = "MovementProfile")]
    public sealed class MovementProfile : ScriptableObject
    {
        [SerializeField] private MovementProfileKind kind = MovementProfileKind.Directional;
        [SerializeField, Min(0.01f)] private float pointAndClickMaxRange = 1000.0f;

        public MovementProfileKind Kind => kind;
        public float PointAndClickMaxRange => pointAndClickMaxRange;
    }
}
