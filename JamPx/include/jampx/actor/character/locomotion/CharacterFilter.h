#pragma once
#include "jampx/PhysicsQueryFilter.h"


namespace jam::px
{
	struct DefaultCharacterFilterPolicy
	{
        // Runtime knobs 

        bool ignoreRemote   = false;
        bool ignoreGhost    = false;
        bool unknownDefault = true;     // fallback for if userData isn't set

        bool ShouldCollide(const PxController& a, const PxController& b) const noexcept
        {
            // self check (usually not needed; PhysX won't pass same object, but safe)
            if (&a == &b) return false;

            const CharacterUserData* ua = GetCharacterUserData(a);
            const CharacterUserData* ub = GetCharacterUserData(b);

            if (!ua || !ub)
                return unknownDefault;

            if (ignoreGhost && (ua->isGhost || ub->isGhost))
                return false;

            if (ignoreRemote && (ua->isRemote || ub->isRemote))
                return false;

            return true;
        }
	};


	template<typename PolicyT = DefaultCharacterFilterPolicy>
	struct CharacterFilterCallbackT final : PxControllerFilterCallback
	{
        static_assert(
            std::is_invocable_r_v<bool, decltype(&PolicyT::ShouldCollide), const PolicyT&, const PxController&, const PxController&>,
            "PolicyT must implement: bool ShouldCollide(const PxController&, const PxController&) const");

		PolicyT policy{};

		explicit CharacterFilterCallbackT(PolicyT p) : policy(std::move(p)) {}

        /// @return true: CCT-CCT collsion enabled
        /// @return false: ignore each other
		bool filter(const PxController& a, const PxController& b) override
		{
            return policy.ShouldCollide(a, b);
		}
	};




    struct DefaultCharacterBehaviorPolicy
    {
        PxControllerBehaviorFlags BehaviorForShape(const PxShape& shape, const PxActor& /*actor*/) const noexcept
        {
            const PxFilterData fd = shape.getQueryFilterData();

            ShapeQueryFlag::Flags flags(fd.word3);

            if (flags & ShapeQueryFlag::RIDEABLE)
                return PxControllerBehaviorFlag::eCCT_CAN_RIDE_ON_OBJECT | PxControllerBehaviorFlag::eCCT_SLIDE;

            if (flags & ShapeQueryFlag::WORLD_STATIC)
                return PxControllerBehaviorFlag::eCCT_SLIDE;

            return PxControllerBehaviorFlags(0);
        }

        PxControllerBehaviorFlags BehaviorForController(const PxController& /*other*/) const noexcept
        {
            // other CCT -> no ridable
            return PxControllerBehaviorFlags(0);
        }

        PxControllerBehaviorFlags BehaviorForObstacle(const PxObstacle& /*ob*/) const noexcept
        {
            // precise: not used obstacle in JamPx
            return PxControllerBehaviorFlags(0);
        }
    };



    template<typename PolicyT = DefaultCharacterBehaviorPolicy>
    struct CharacterBehaviorCallbackT final : PxControllerBehaviorCallback
    {
        static_assert(
            std::is_invocable_r_v<PxControllerBehaviorFlags, decltype(&PolicyT::BehaviorForShape), const PolicyT&, const PxShape&, const PxActor&>,
            "PolicyT must implement: PxControllerBehaviorFlags BehaviorForShape(const PxShape&, const PxActor&) const");

        static_assert(
            std::is_invocable_r_v<PxControllerBehaviorFlags, decltype(&PolicyT::BehaviorForController), const PolicyT&, const PxController&>,
            "PolicyT must implement: PxControllerBehaviorFlags BehaviorForController(const PxController&) const");

        static_assert(
            std::is_invocable_r_v<PxControllerBehaviorFlags, decltype(&PolicyT::BehaviorForObstacle), const PolicyT&, const PxObstacle&>,
            "PolicyT must implement: PxControllerBehaviorFlags BehaviorForObstacle(const PxObstacle&) const");


        PolicyT policy{};

        explicit CharacterBehaviorCallbackT(PolicyT p) : policy(std::move(p)) {}

        PxControllerBehaviorFlags getBehaviorFlags(const PxShape& shape, const PxActor& actor) override
        {
            return policy.BehaviorForShape(shape, actor);
        }

        PxControllerBehaviorFlags getBehaviorFlags(const PxController& controller) override
        {
            return policy.BehaviorForController(controller);
        }
    	
    	PxControllerBehaviorFlags getBehaviorFlags(const PxObstacle& obstacle) override
        {
            return policy.BehaviorForObstacle(obstacle);
        }
    };



    struct CharacterShapeHitEvent
    {
        const PxController*     controller  = nullptr;
        const PxShape*          shape       = nullptr;
        const PxActor*          actor       = nullptr;

        PxVec3                  worldPos    = PxVec3(physx::PxZero);
        PxVec3                  worldNormal = PxVec3(physx::PxZero);
        PxF32                   length      = 0.0f;

        // (optional)
        PxFilterData            shapeQfd{};
    };

    struct CharacterCCTHitEvent
    {
        const PxController*     a           = nullptr;
        const PxController*     b           = nullptr;
        PxVec3                  worldNormal = PxVec3(physx::PxZero);
    };

    struct ICharacterHitSink
    {
        virtual ~ICharacterHitSink() = default;

        virtual void OnShapeHit(const CharacterShapeHitEvent& event) = 0;
        virtual void OnControllerHit(const CharacterCCTHitEvent& event) = 0;
    };

    struct DefaultCharacterHitReportPolicy
    {
        ICharacterHitSink* sink = nullptr;

        bool Enabled() const noexcept
        {
	        return sink != nullptr;
        }

        bool AcceptShapeHit(const PxControllerShapeHit& hit) const noexcept
        {
            if (hit.shape && (hit.shape->getFlags() & PxShapeFlag::eTRIGGER_SHAPE))
                return false;

            return true;
        }

        bool AcceptControllerHit(const PxControllersHit& hit) const noexcept
        {
            return true;
        }

        void EmitShapeHit(const PxControllerShapeHit& hit) const
        {
            if (!Enabled()) return;

            CharacterShapeHitEvent e;
            e.controller    = hit.controller;
            e.shape         = hit.shape;
            e.actor         = hit.actor;
            e.worldPos      = toVec3(hit.worldPos);
            e.worldNormal   = hit.worldNormal;
            e.length        = hit.length;
            e.shapeQfd      = hit.shape ? hit.shape->getQueryFilterData() : PxFilterData{};

            sink->OnShapeHit(e);
        }

        void EmitControllerHit(const PxControllersHit& hit) const
        {
            if (!Enabled()) return;

            CharacterCCTHitEvent e;
            e.a             = hit.controller;
            e.b             = hit.other;
            e.worldNormal   = hit.worldNormal;

            sink->OnControllerHit(e);
        }
    };


    template<class PolicyT = DefaultCharacterHitReportPolicy>
    class CharacterHitReportT final : public PxUserControllerHitReport
    {
    public:
        PolicyT policy{};

        explicit CharacterHitReportT(PolicyT p) : policy(std::move(p)) {}

        void onShapeHit(const PxControllerShapeHit& hit) override
        {
            if (!policy.Enabled()) return;
            if (!policy.AcceptShapeHit(hit)) return;
            policy.EmitShapeHit(hit);
        }

        void onControllerHit(const PxControllersHit& hit) override
        {
            if (!policy.Enabled()) return;
            if (!policy.AcceptControllerHit(hit)) return;
            policy.EmitControllerHit(hit);
        }

        void onObstacleHit(const PxControllerObstacleHit& /*hit*/) override
        {
            // JamPx: obstacle 미사용 전제 → no-op
        }
    };

}
