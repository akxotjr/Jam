#pragma once
#include "jampx/geometry/Curve.h"


namespace jam::px
{
    struct KinematicCommon
    {
        bool        computeDerivedVel = true;
        bool        carryRiders       = true;
        bool        sweep             = false;      // sweep 이동 vs teleport
        float       maxSpeed          = 1000.f;
    };



    // ---------------------------------------------------------------
	// Waipoint Source : path-driven
	// ---------------------------------------------------------------


    enum class eWaypointLoop : uint8
    {
        Once,            // A->B->C 
        Loop,            // A->B->C->A
        PingPong,        // A->B->C->B->A
    };

    struct KinematicWaypoint
    {
        PxTransform         pose            = PxTransform(physx::PxIdentity);
        float               pauseDuration   = 0.f;
    };

    struct WaypointSource
    {
        std::vector<KinematicWaypoint> waypoints;

        float               speed           = 5.f;
        eWaypointLoop       loopMode        = eWaypointLoop::Loop;

        // Ease per segment
        bool                useEaseProfile  = false;
        eEaseType           easeType        = eEaseType::Linear;
        EaseProfile         easeProfile     = {};
    };


    // ---------------------------------------------------------------
    // Curve Source : path-driven
    // ---------------------------------------------------------------

    struct CurveSource
    {
        std::vector<PxVec3> controlPoints; 
        eCurveType          type            = eCurveType::CatmullRom;
        float               speed           = 5.f;
        float               duration        = 3.f;
        bool                loop            = true;
        uint32              buildSegments   = 64;

        bool                useEaseProfile  = false;
        eEaseType           easeType        = eEaseType::SmoothStep;
        EaseProfile         easeProfile     = {};

        float               alpha           = 0.5f;     // only CatmullRom : alpha(0 = uniform, 0.5 = centripetal, 1 = chordal)

        uint32              degree          = 3;        // only BSpline : degree
    };




    // ---------------------------------------------------------------
	// Orbit Source : path-driven
	// ---------------------------------------------------------------

    enum class eOrbitPlaneMode : uint8
    {
	    XY,
        XZ,
        YZ,
        Custom
    };

    enum class eOrbitCenterMode : uint8
    {
	    FixedPoint,
        FollowTarget,
    };

    enum class eOrbitRadiusMode : uint8
    {
	    Circle,
        Ellipse,
    };

    enum class eOrbitOrientationMode : uint8
    {
	    KeepRotation,           // rotation unchanged
        FaceCenter,             // look at center
        OrientAlongVelocity,    // look at tangent direction
    };

    enum class eOrbitEndMode : uint8_t
    {
        Loop,
        PingPong,
        Clamp,
    };

    struct OrbitSource
    {
        // ---- center ----
        eOrbitCenterMode        centerMode          = eOrbitCenterMode::FixedPoint;
        PxVec3                  fixedCenter         = PxVec3(physx::PxZero);
        PxVec3                  targetOffset        = PxVec3(physx::PxZero);   // using centerMode = FollowTarget

        // ---- plane ----
        eOrbitPlaneMode         planeMode           = eOrbitPlaneMode::XZ;
        PxVec3                  customPlaneNormal   = PxVec3(0.f, 1.f, 0.f);

        // ---- radius ----
        eOrbitRadiusMode        radiusMode          = eOrbitRadiusMode::Circle;
        float                   radius              = 3.0f;
        PxVec2                  ellipseRadius       = PxVec2(3.0f, 2.0f);

        // ---- angle progression ----
        float                   initialAngleRad     = 0.0f;
        float                   angularSpeedRad     = 1.0f;

        // ---- end mode ----
        eOrbitEndMode           endMode             = eOrbitEndMode::Loop;


        // PingPong/Clamp의 각도 범위
        float                   minAngleRad         = 0.0f;
        float                   maxAngleRad         = physx::PxTwoPi;

        // ---- orientation ----
        eOrbitOrientationMode   orientationMode     = eOrbitOrientationMode::OrientAlongVelocity;
        PxQuat                  initialRotation     = PxQuat(physx::PxIdentity);

        // PingPong/Clamp 모드에서 끝점 부근 ease
        bool                    useEaseAtEnds       = false;
        EaseProfile             endEaseProfile      = {};

        // ---- options ----
        bool                    computeDerivedVelocity = true;
    };



    // ---------------------------------------------------------------
	// Follow Source : target-driven
	// ---------------------------------------------------------------

    enum class eFollowOffsetSpace : uint8
    {
	    TargetLocal,
        World
    };

    enum class eFollowRotationMode : uint8
    {
	    KeepWorldRotation,             
        MatchTargetRotation,
        LookAtTarget,
        OrientAlongVelocity
    };

    struct FollowSource
    {
        ObjectId            targetId                = INVALID_OBJ_ID;

        PxVec3              offset                  = PxVec3(physx::PxZero);
        eFollowOffsetSpace  offsetSpace             = eFollowOffsetSpace::TargetLocal;

        float               positionFollowSpeed     = 10.f;
        float               rotationFollowSpeed     = 10.f;

        float               maxLinearSpeed          = 1000.f;
        float               maxAngularSpeed         = physx::PxPi;

        eFollowRotationMode rotationMode            = eFollowRotationMode::KeepWorldRotation;

        bool                snapIfTargetMissing     = false;
        bool                keepLastPoseIfMissing   = true;

        bool                computeDerivedVelocity = true;
    };


    // ---------------------------------------------------------------
    // Network Source : target-driven
    // ---------------------------------------------------------------

    struct NetworkPoseSource
    {
        bool                computeDerivedVelocity = false;
    };





    //todo
    struct RootMotionSource
    {
        std::string clipId;
        float       playbackSpeed = 1.f;
    };

    //todo
    struct ScriptTimelineSource
    {
        std::string timelineAssetId;
        float       playbackSpeed   = 1.f;
        bool        loop            = false;
    };



    using PoseSource = std::variant<
	    WaypointSource, 
	    CurveSource, 
	    OrbitSource, 
	    FollowSource,
	    RootMotionSource,
	    NetworkPoseSource, 
	    ScriptTimelineSource
    >;

    struct KinematicDriverConfig
    {
        KinematicCommon common;
        PoseSource      source;
    };


} // namespace jam::px
