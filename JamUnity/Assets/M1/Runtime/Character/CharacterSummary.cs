namespace JamUnity.Runtime.Client
{
    public readonly struct CharacterSummary
    {
        public readonly ulong CharacterId;
        public readonly string Name;
        public readonly ulong ActorArchetypeKey;

        public bool IsValid => CharacterId != 0
            && !string.IsNullOrEmpty(Name)
            && ActorArchetypeKey != 0;

        public CharacterSummary(ulong characterId, string name, ulong actorArchetypeKey)
        {
            CharacterId = characterId;
            Name = name ?? string.Empty;
            ActorArchetypeKey = actorArchetypeKey;
        }

        internal static CharacterSummary FromFlatBuffer(m1.fb.fbCharacterSummary value)
        {
            return new CharacterSummary(value.CharacterId, value.Name, value.ActorArchetypeKey);
        }
    }
}
