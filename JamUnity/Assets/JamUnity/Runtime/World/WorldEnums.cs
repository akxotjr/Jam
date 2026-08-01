using System.Runtime.Serialization;

namespace JamUnity.World.Runtime
{
    public enum eWorldRoutePolicy
    {
        [EnumMember(Value = "spread_by_load")]
        SpreadByLoad = 0,
    }
}
