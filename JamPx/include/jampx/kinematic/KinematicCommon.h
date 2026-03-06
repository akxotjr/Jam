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
    // Pose Sources
    // ---------------------------------------------------------------

    enum class eWaypointLoop : uint8
    {
	    Once,
        Loop,
        PingPong,
    };

    struct KinematicWaypoint
    {
        PxTransform     pose            = PxTransform(PxIdentity);
        float           pauseDuration   = 0.f;
    };

    struct WaypointSource
    {
        std::vector<KinematicWaypoint> waypoints;

        float           speed       = 5.f;
        eWaypointLoop   loopMode    = eWaypointLoop::Loop;
    };




    struct SplineSource
    {
        std::vector<PxVec3> controlPoints; 
        eCurveType          type            = eCurveType::CatmullRom;
        float               speed           = 5.f;
        bool                loop            = true;
        uint32              buildSegments   = 64;

        float               alpha           = 0.5f;     // only CatmullRom : alpha(0 = uniform, 0.5 = centripetal, 1 = chordal)

        uint32              degree          = 3;        // only BSpline : degree
    };


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
        Vec3        center          = Vec3::Zero();
        float       angularSpeed    = 1.f;       // rad/s
        Vec3        axis            = { 0, 1, 0 };    // axis of revolution
        float       startAngle      = 0.f;     // rad


        // ---- center ----
        eOrbitCenterMode centerMode = eOrbitCenterMode::FixedPoint;
        PxVec3 fixedCenter = PxVec3(PxZero);
        PxVec3 targetOffset = PxVec3(PxZero);   // using centerMode = FollowTarget

        // ---- plane ----
        eOrbitPlaneMode planeMode = eOrbitPlaneMode::XZ;
        PxVec3 customPlaneNormal = PxVec3(0.f, 1.f, 0.f);

        // ---- radius ----
        eOrbitRadiusMode radiusMode = eOrbitRadiusMode::Circle;
        float   radius = 3.0f;
        PxVec2 ellipseRadius = PxVec2(3.0f, 2.0f);

        // ---- angle progression ----
        float initialAngleRad = 0.0f;
        float angularSpeedRad = 1.0f;

        // ---- end mode ----
        eOrbitEndMode endMode = eOrbitEndMode::Loop;


        // PingPong/Clamp의 각도 범위
        float            minAngleRad = 0.0f;
        float            maxAngleRad = PxTwoPi;

        // ---- orientation ----
        eOrbitOrientationMode orientationMode = eOrbitOrientationMode::OrientAlongVelocity;
        PxQuat                initialRotation = PxQuat(PxIdentity);


        // ---- options ----
        bool computeDerivedVelocity = true;
    };





    struct FollowSource
    {
        ObjectId    targetId        = INVALID_OBJ_ID;
        Vec3        offset          = Vec3::Zero();         // local offset
        float       lerpSpeed       = 10.f;
    };


    struct RootMotionSource
    {
        std::string clipId;
        float       playbackSpeed   = 1.f;
    };

    struct NetworkPoseSource
    {
        float       interpSpeed     = 20.f;
    };

    struct ScriptTimelineSource
    {
        std::string timelineAssetId;
        float       playbackSpeed   = 1.f;
        bool        loop            = false;
    };

    using PoseSource = std::variant<
	    WaypointSource, 
	    SplineSource, 
	    OrbitSource, 
	    FollowSource,
	    RootMotionSource,
	    NetworkPoseSource, 
	    ScriptTimelineSource
    >;

    struct KinematicMoveConfig
    {
        KinematicCommon common;
        PoseSource      source;
    };
}
