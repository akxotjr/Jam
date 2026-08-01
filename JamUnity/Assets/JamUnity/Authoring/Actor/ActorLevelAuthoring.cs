using UnityEngine;



namespace JamUnity.Authoring.Actor
{
    public sealed class ActorLevelAuthoring : MonoBehaviour
    {
		public const uint MaxAuthoredSlot = 0x000F_FFFF;
		private const uint InitialGeneration = 1u << 20;

        [SerializeField] private bool               exportEnabled   = true;
        // actorId is a deterministic per-level local id, not a hashed global asset key.
		[SerializeField] private uint               actorId    = 0;
        [SerializeField] private ActorArchetypeData actorArchetype;
    
        public bool ExportEnabled => exportEnabled;
        public uint ActorId => actorId;
        public ActorArchetypeData ActorArchetype => actorArchetype;

        public void Configure(uint value, ActorArchetypeData archetype)
        {
            actorId = value;
            actorArchetype = archetype;
        }

		public void SetActorId(uint value)
		{
			actorId = value;
		}

		public static uint MakeInitialActorId(uint slot)
		{
			return slot == 0 || slot > MaxAuthoredSlot ? 0 : InitialGeneration | slot;
		}

		public static bool IsCanonicalActorId(uint value)
		{
			return (value >> 20) == 1 && (value & MaxAuthoredSlot) != 0;
		}

		public static uint NormalizeLegacyActorId(uint value)
		{
			return value > 0 && value <= MaxAuthoredSlot ? MakeInitialActorId(value) : value;
		}
    }
} // namespace JamUnity.Authoring.Actor
