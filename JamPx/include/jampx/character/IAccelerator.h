#pragma once


namespace jam::px
{
    struct WishMovement
    {
        Vec3  dir{ 0,0,0 };      // normalized or zero
        float speed = 0.0f;             // wish speed (units/s)
    };

    class IAccelerator
    {
    public:
        virtual ~IAccelerator() = default;

        virtual WishMovement    BuildWishMovement(const MoveIntent& in) const = 0;
        virtual void            Integrate(MovementState& st, const WishMovement& wish, float dt) const = 0;
    };
}
