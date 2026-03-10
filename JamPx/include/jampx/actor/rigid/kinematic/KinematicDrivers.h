#pragma once

#include "jampx/geometry/Curve.h"
#include "jampx/actor/rigid/kinematic/IKinematicDriver.h"
#include "jampx/actor/rigid/kinematic/KinematicCommon.h"

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

        bool                m_done        = false;
        PxTransform         m_pose        = PxTransform(physx::PxIdentity);

        int32               m_segIndex    = 0;
        int32               m_dir         = 1;      // +1 / -1 (only using PingPong)
        float               m_segProgress = 0.f;    // [0, 1]
        float               m_pauseTimer  = 0.f;
    };




    // -----------------------------------------------------------
    // Curve
    // -----------------------------------------------------------

    class CurveKinematicDriver : public IKinematicDriver
    {
        explicit CurveKinematicDriver(KinematicCommon common, CurveSource src);

        PxTransform             Tick(float dt) override;
        bool                    IsDone() const override { return m_done; }

    private:
        void                    BuildArchLengthLUT();
        float                   ArcLengthToT(float arcLen) const;

    private:
        KinematicCommon         m_common        = {};
        CurveSource             m_src           = {};

        std::unique_ptr<Curve>  m_curve;                    // CatmullRom / BSpline / Bezier
        std::vector<float>      m_lut;                      // accumulative distance LUT based on m_nodes [0...n]
        
        bool                    m_done          = false;
        PxTransform             m_pose          = PxTransform(physx::PxIdentity);
        float                   m_elapsedTime   = 0.f;
    	float                   m_totalLength   = 0.f;      
        float                   m_arcPos        = 0.f;      // current distance
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
        PxTransform         m_pose          = PxTransform(physx::PxIdentity);

        float               m_angle         = 0.f;
        float               m_dir           = 1.0f;     // +1.f / -1.f : for PingPong 

        // orbit plane basis(pre calculate: Gram-schmidt algorithm)
        PxVec3              m_axisN         = { 0.f, 1.f, 0.f };    // plane normal (normalized)
        PxVec3              m_basisR        = { 1.f, 0.f, 0.f };    // angle = 0 
        PxVec3              m_basisF        = { 0.f, 0.f, 1.f };    // angle = pi/2

        PxVec3              m_dynamicCenter = PxVec3(physx::PxZero);   // only FollowTarget
    };




    /** -----------------------------------------------------------
    * Follow
     -----------------------------------------------------------
     */

	/// Lookup callback: ObjectId -> PxTransform 
	/// If target is not found, returns std::nullopt.
    using TargetPoseResolver = std::function<std::optional<PxTransform>(ObjectId)>;

    class FollowKinematicDriver : public IKinematicDriver
    {
    public:
        FollowKinematicDriver(KinematicCommon common, FollowSource src, TargetPoseResolver resolver);

        PxTransform         Tick(float dt) override;
        bool                IsDone() const override { return false; }

    private:
        PxQuat              ComputeTargetRotation(const PxTransform& targetPose, const PxVec3& desiredPos) const;

    private:
        KinematicCommon     m_common            = {};
        FollowSource        m_src               = {};
        TargetPoseResolver  m_resolver          = nullptr;

        PxTransform         m_pose              = PxTransform(physx::PxIdentity);
        bool                m_hasValidTarget    = false;    // whether ever found a target
        bool                m_targetWasMissing  = false;    // whether no target in the previous frame
    };

    // -----------------------------------------------------------
	// NetworkPose
	// -----------------------------------------------------------

    class NetworkPoseKinematicDriver : public IKinematicDriver
    {
    public:
        NetworkPoseKinematicDriver(KinematicCommon common, NetworkPoseSource src);

        PxTransform         Tick(float dt) override { return m_pose; }

        void                SetAuthoritativePose(const PxTransform& pose) { m_pose = pose; }
        void                SetAuthoritativeLinearVelocity(const PxVec3& v) { m_linearVel = v; }
        void                SetAuthoritativeAngularVelocity(const PxVec3& w) { m_angularVel = w; }

    private:
        KinematicCommon     m_common     = {};
        NetworkPoseSource   m_src        = {};
        PxTransform         m_pose       = PxTransform(physx::PxIdentity);
        PxVec3              m_linearVel  = PxVec3(physx::PxZero);
        PxVec3              m_angularVel = PxVec3(physx::PxZero);
    };





    





} // namespace jam::px
