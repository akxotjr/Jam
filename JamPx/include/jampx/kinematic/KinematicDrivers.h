#pragma once

#include "jampx/geometry/Curve.h"
#include "jampx/kinematic/IKinematicDriver.h"
#include "jampx/kinematic/KinematicCommon.h"

namespace jam::px
{

    // -----------------------------------------------------------
    // Waypoint
    // -----------------------------------------------------------

    class WaypointKinematicDriver : public IKinematicDriver
    {
    public:
        explicit WaypointKinematicDriver(KinematicCommon common, WaypointSource src);

        PxTransform         Tick(float dt) override;
        bool                IsDone() const override { return m_done; }

    private:
        int32               NextIndex() const;
        void                AdvanceSegment();

    private:
        KinematicCommon     m_common      = {};
        WaypointSource      m_src         = {};

        int32               m_segIndex    = 0;
        int32               m_dir         = 1;      // +1 / -1 (only using PingPong)
        float               m_segProgress = 0.f;    // [0, 1]
        float               m_pauseTimer  = 0.f;
        bool                m_done        = false;
        PxTransform         m_pose        = PxTransform(PxIdentity);
    };




    // -----------------------------------------------------------
    // Spline
    // -----------------------------------------------------------

    class SplineKinematicDriver : public IKinematicDriver
    {
        explicit SplineKinematicDriver(KinematicCommon common, SplineSource src);

        PxTransform         Tick(float dt) override;

    private:
        void                BuildArchLengthLUT();

        float               ArcLengthToT(float arcLen) const;

    private:
        KinematicCommon     m_common        = {};
        SplineSource        m_src           = {};

        unique_ptr<Curve>   m_curve;                    // CatmullRom / BSpline / Bezier
        vector<float>       m_lut;                      // accumulative distance LUT based on m_nodes [0...n]
        float               m_totalLength   = 0.f;      
        float               m_arcPos        = 0.f;      // current distance
    };



    // -----------------------------------------------------------
    // Orbit
    // -----------------------------------------------------------

    class OrbitKinematicDriver : public IKinematicDriver
    {
    public:
        explicit OrbitKinematicDriver(KinematicCommon common, OrbitSource src);


        PxTransform         Tick(float dt) override;
        bool                IsDone() const override { return m_done; }

        void                SetDynamicCenter(const PxVec3& center) { m_dynamicCenter = center; }

    private:
        PxVec3              ComputeCenter() const;
        PxVec3              ComputePosition(float angle) const;
        PxVec3              ComputeTangent(float angle) const;
        PxQuat              ComputeRotation(float angle, const PxVec3& pos) const;
        void                AdvanceAngle(float dt);

    private:
        KinematicCommon     m_common        = {};
        OrbitSource         m_src           = {};

        bool                m_done          = false;

        float               m_angle         = 0.f;
        float               m_dir           = 1.0f;     // +1.f / -1.f : for PingPong 

        // orbit plane basis(pre calculate: Gram-schmidt algorithm)
        PxVec3              m_axisN         = { 0.f, 1.f, 0.f };    // plane normal (normalized)
        PxVec3              m_basisR        = { 1.f, 0.f, 0.f };    // angle = 0 
        PxVec3              m_basisF        = { 0.f, 0.f, 1.f };    // angle = pi/2

        PxVec3              m_dynamicCenter = PxVec3(PxZero);   // only FollowTarget
    };

    // -----------------------------------------------------------
    // Orbit
    // -----------------------------------------------------------

    class FollowKinematicDriver : public IKinematicDriver
    {
    public:
        FollowKinematicDriver(KinematicCommon common, FollowSource src, IPhysicsFacade* facade);

        PxTransform         Tick(float dt) override;

    private:
        KinematicCommon     m_common;
        FollowSource        m_src;
        IPhysicsFacade*     m_facade = nullptr;
        Transform           m_current{};
    };


} // namespace jam::px
