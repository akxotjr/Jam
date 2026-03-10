#pragma once

#include "jampx/api/PhysicsTypes.h"
#include "jampx/character/CharacterMovementTypes.h"

namespace jam::px
{
    // 매 프레임 AIControllerComponent가 채워서 Evaluate()에 넘기는 스냅샷
    struct AIContext
    {
        Vec3                selfPos             = Vec3::Zero();
        Vec3                selfVel             = Vec3::Zero();
        float               selfYaw             = 0.f;
        bool                grounded            = false;
        bool                canJump             = false;
        bool                canDash             = false;

        bool                hasTarget           = false;
        Vec3                targetPos           = Vec3::Zero();
        Vec3                targetVel           = Vec3::Zero();
        bool                hasLOS              = false;

        bool                hasPath             = false;
        std::vector<Vec3>   pathPoints          = {};
        uint32              currentPathIndex    = 0;

        void*               userData            = nullptr;
    };


    struct AIDesiredAction
    {
        bool                stopMovement        = false;
        bool                wantsMove           = false;
        Vec3                moveDir             = Vec3::Zero();     // world-space, normalized
        float               facingYaw           = 0.f;              // 바라볼 방향 (rad)
        eGait               gait                = eGait::Run;
        eStance             stance              = eStance::Standing;
        bool                jump                = false;
        bool                dash                = false;
    };

    enum class eAIDecisionStatus : uint8
    {
	    Success,
        Failed,
        Running
    };

	class IAIDecisionModel
	{
	public:
		virtual ~IAIDecisionModel() = default;

        virtual void                Reset() {}
        virtual eAIDecisionStatus   Evaluate(const AIContext& ctx, OUT AIDesiredAction& desired) = 0;
	};
}
