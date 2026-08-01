using System;

namespace JamUnity.Authoring.Physics
{
    [Serializable]
    public class FilterData
    {
        public uint word0;
        public uint word1;
        public uint word2;
        public uint word3;
    }

    [Flags]
    public enum eSimulationCategory : uint
    {
        None         = 0,
        WorldStatic  = 1u << 0,
        WorldDynamic = 1u << 1,
        Character    = 1u << 2,
        Projectile   = 1u << 3,
        Sensor       = 1u << 4,
    }

    [Flags]
    public enum eSimulationUserFlag : uint
    {
        None                   = 0,
        NotifyTouchFound       = 1u << 0,
        NotifyTouchLost        = 1u << 1,
        NotifyTouchPersists    = 1u << 2,
        NotifyTouchCcd         = 1u << 3,
        ModifyContacts         = 1u << 4,
        NotifyContactPoints    = 1u << 5,
        ThresholdForceFound    = 1u << 6,
        ThresholdForcePersists = 1u << 7,
        ThresholdForceLost     = 1u << 8,
        PreSolverVelocity      = 1u << 9,
        PostSolverVelocity     = 1u << 10,
        ContactEventPose       = 1u << 11,
    }

    [Flags]
    public enum eQueryCategory : uint
    {
        None      = 0,
        World     = 1u << 0,
        Character = 1u << 1,
        Hitbox    = 1u << 2,
        Trigger   = 1u << 3,
    }

    [Flags]
    public enum eShapeQueryFlag : uint
    {
        None           = 0,
        IsHead         = 1u << 0,
        Penetrable     = 1u << 1,
        NoLosBlock     = 1u << 2,
        WorldStatic    = 1u << 8,
        WorldDynamic   = 1u << 9,
        WorldKinematic = 1u << 10,
        Rideable       = 1u << 11,
    }

    [Serializable]
    public sealed class SimulationFilterData
    {
        private const uint KnownCategoryBits =
            (uint)(eSimulationCategory.WorldStatic |
                   eSimulationCategory.WorldDynamic |
                   eSimulationCategory.Character |
                   eSimulationCategory.Projectile |
                   eSimulationCategory.Sensor);

        private const uint KnownUserFlagBits =
            (uint)(eSimulationUserFlag.NotifyTouchFound |
                   eSimulationUserFlag.NotifyTouchLost |
                   eSimulationUserFlag.NotifyTouchPersists |
                   eSimulationUserFlag.NotifyTouchCcd |
                   eSimulationUserFlag.ModifyContacts |
                   eSimulationUserFlag.NotifyContactPoints |
                   eSimulationUserFlag.ThresholdForceFound |
                   eSimulationUserFlag.ThresholdForcePersists |
                   eSimulationUserFlag.ThresholdForceLost |
                   eSimulationUserFlag.PreSolverVelocity |
                   eSimulationUserFlag.PostSolverVelocity |
                   eSimulationUserFlag.ContactEventPose);

        public eSimulationCategory category;
        public eSimulationCategory mask;
        public eSimulationUserFlag userFlags;
        public uint customCategoryBits;
        public uint customMaskBits;
        public uint customUserFlagBits;
        public uint customWord3;

        public uint Word0 => (uint)category | (customCategoryBits & ~KnownCategoryBits);
        public uint Word1 => (uint)mask | (customMaskBits & ~KnownCategoryBits);
        public uint Word2 => (uint)userFlags | (customUserFlagBits & ~KnownUserFlagBits);
        public uint Word3 => customWord3;

        public static SimulationFilterData FromWords(uint word0, uint word1, uint word2, uint word3)
        {
            return new SimulationFilterData
            {
                category           = (eSimulationCategory)(word0 & KnownCategoryBits),
                mask               = (eSimulationCategory)(word1 & KnownCategoryBits),
                userFlags          = (eSimulationUserFlag)(word2 & KnownUserFlagBits),
                customCategoryBits = word0 & ~KnownCategoryBits,
                customMaskBits     = word1 & ~KnownCategoryBits,
                customUserFlagBits = word2 & ~KnownUserFlagBits,
                customWord3        = word3,
            };
        }
    }

    [Serializable]
    public sealed class ShapeQueryFilterData
    {
        private const uint KnownCategoryBits =
            (uint)(eQueryCategory.World |
                   eQueryCategory.Character |
                   eQueryCategory.Hitbox |
                   eQueryCategory.Trigger);

        private const uint KnownShapeFlagBits =
            (uint)(eShapeQueryFlag.IsHead |
                   eShapeQueryFlag.Penetrable |
                   eShapeQueryFlag.NoLosBlock |
                   eShapeQueryFlag.WorldStatic |
                   eShapeQueryFlag.WorldDynamic |
                   eShapeQueryFlag.WorldKinematic |
                   eShapeQueryFlag.Rideable);

        public eQueryCategory category;
        public uint channel;
        public uint sublayer;
        public uint tag;
        public uint team;
        public uint part;
        public uint role;
        public eShapeQueryFlag flags;
        public uint customCategoryBits;
        public uint customFlagBits;

        public uint Word0 => (uint)category | (customCategoryBits & ~KnownCategoryBits);
        public uint Word1 => (channel & 0xFFu) | ((sublayer & 0xFFu) << 8) | ((tag & 0xFFFFu) << 16);
        public uint Word2 => (team & 0xFFFFu) | ((part & 0xFFu) << 16) | ((role & 0xFFu) << 24);
        public uint Word3 => (uint)flags | (customFlagBits & ~KnownShapeFlagBits);

        public static ShapeQueryFilterData FromWords(uint word0, uint word1, uint word2, uint word3)
        {
            return new ShapeQueryFilterData
            {
                category           = (eQueryCategory)(word0 & KnownCategoryBits),
                channel            = word1 & 0xFFu,
                sublayer           = (word1 >> 8) & 0xFFu,
                tag                = (word1 >> 16) & 0xFFFFu,
                team               = word2 & 0xFFFFu,
                part               = (word2 >> 16) & 0xFFu,
                role               = (word2 >> 24) & 0xFFu,
                flags              = (eShapeQueryFlag)(word3 & KnownShapeFlagBits),
                customCategoryBits = word0 & ~KnownCategoryBits,
                customFlagBits     = word3 & ~KnownShapeFlagBits,
            };
        }
    }
} // namespace JamUnity.Authoring.Physics
