#pragma once


namespace jam::px
{
    /// @brief external movement layer (injected by animation/gameplay/etc.)
    enum class eExternalMovementLayer : uint8
    {
        NONE,
        ROOT_MOTION,
        DASH,
        KNOCKBACK,
        PLATFORM,
        SCRIPT
    };

    struct ExternalMoveRequest
    {
        eExternalMovementLayer	layer = eExternalMovementLayer::NONE;

        // velocity add (impulses / dash / knockback)
        Vec3					addVelocity = Vec3::Zero();

        // displacement add (root motion / moving flatform / etc.)
        Vec3					addDisplacement = Vec3::Zero();

        // Override locomotion (Quake integrator) on XZ only (gravity/jump remains)
        bool					overrideLocomotion = false;

        // [0..1], 1 = disable locomotion XZ fully
        float					overrideWeight = 1.0f;
    };


	class ExternalMoveAccumulator
	{
    public:
        void        Clear();
        void        Add(const ExternalMoveRequest& r);

        Vec3        GetAddVelocity()        const { return m_addVel; }
        Vec3        GetAddDisplacement()    const { return m_addDisp; }
        bool        IsOverrideLocomotion()  const { return m_override; }
        float       GetOverrideWeight()     const { return m_overrideWeight; }

    private:
        Vec3        m_addVel            = Vec3::Zero();
        Vec3        m_addDisp           = Vec3::Zero();
        bool        m_override          = false;
        float       m_overrideWeight    = 0.0f;
	};
}
