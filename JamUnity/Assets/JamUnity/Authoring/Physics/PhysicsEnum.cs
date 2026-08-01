using System;
using System.Runtime.Serialization;

namespace JamUnity.Authoring.Physics
{
    public enum eShapeType
    {
        [EnumMember(Value = "none")]
        None = 0,
        [EnumMember(Value = "box")]
        Box = 1,
        [EnumMember(Value = "sphere")]
        Sphere = 2,
        [EnumMember(Value = "capsule")]
        Capsule = 3,
        [EnumMember(Value = "plane")]
        Plane = 4,
        [EnumMember(Value = "triangle_mesh")]
        TriangleMesh = 5,
        [EnumMember(Value = "convex_mesh")]
        ConvexMesh = 6,
    }
    
    public enum eShapeFlag
    {
        [EnumMember(Value = "simulation")]
        Simulation = 0,
        [EnumMember(Value = "simulation_only")]
        SimulationOnly = 1,
        [EnumMember(Value = "trigger")]
        Trigger = 2,
        [EnumMember(Value = "trigger_only")]
        TriggerOnly = 3,
        [EnumMember(Value = "query_only")]
        QueryOnly = 4,
    }
    
    public enum eMeshType
    {
        [EnumMember(Value = "triangle")]
        Triangle = 0,
        [EnumMember(Value = "convex")]
        Convex = 1,
    }
    
    public enum eActorType
    {
        [EnumMember(Value = "generic")]
        Generic = 0,
        [EnumMember(Value = "character")]
        Character = 1,
        [EnumMember(Value = "projectile")]
        Projectile = 2,
    }
    
    public enum eBodyType
    {
        [EnumMember(Value = "rigid_body")]
        RigidBody = 0,
        [EnumMember(Value = "character_body")]
        CharacterBody = 1,
    }
    
    public enum eMotionType
    {
        [EnumMember(Value = "none")]
        None = 0,
        [EnumMember(Value = "static")]
        Static = 1,
        [EnumMember(Value = "dynamic")]
        Dynamic = 2,
        [EnumMember(Value = "kinematic")]
        Kinematic = 3,
        [EnumMember(Value = "cct")]
        Cct = 4,
        [EnumMember(Value = "remote_cct")]
        RemoteCct = 5,
    }
    
     [Flags]
    public enum eMotionFlag
    {
        None = 0,
        [EnumMember(Value = "disable_gravity")]
        DisableGravity = 1 << 0,
        [EnumMember(Value = "enable_ccd")]
        EnableCcd = 1 << 1,
        [EnumMember(Value = "lock_linear_x")]
        LockLinearX = 1 << 2,
        [EnumMember(Value = "lock_linear_y")]
        LockLinearY = 1 << 3,
        [EnumMember(Value = "lock_linear_z")]
        LockLinearZ = 1 << 4,
        [EnumMember(Value = "lock_angular_x")]
        LockAngularX = 1 << 5,
        [EnumMember(Value = "lock_angular_y")]
        LockAngularY = 1 << 6,
        [EnumMember(Value = "lock_angular_z")]
        LockAngularZ = 1 << 7,
    }
    
    public enum eCCTPolicy
    {
        [EnumMember(Value = "default")]
        Default = 0,
    }
    
    public enum eRigidBehaviorKind
    {
        [EnumMember(Value = "none")]
        None = 0,
        [EnumMember(Value = "kinematic_driver")]
        KinematicDriver = 1,
        [EnumMember(Value = "projectile")]
        Projectile = 2,
    }
    
    public enum eControllerType
    {
        [EnumMember(Value = "none")]
        None = 0,
        [EnumMember(Value = "player")]
        Player = 1,
        [EnumMember(Value = "ai")]
        AI = 2,
    }
    
    public enum eKinematicSourceType
    {
        [EnumMember(Value = "waypoint")]
        Waypoint = 0,
        [EnumMember(Value = "curve")]
        Curve = 1,
        [EnumMember(Value = "orbit")]
        Orbit = 2,
        [EnumMember(Value = "follow")]
        Follow = 3,
        [EnumMember(Value = "network_pose")]
        NetworkPose = 4,
    }
    
    public enum eKinematicEaseType
    {
        [EnumMember(Value = "linear")]
        Linear = 0,
        [EnumMember(Value = "smooth_step")]
        SmoothStep = 1,
        [EnumMember(Value = "smoother_step")]
        SmootherStep = 2,
        [EnumMember(Value = "in_sine")]
        InSine = 3,
        [EnumMember(Value = "out_sine")]
        OutSine = 4,
        [EnumMember(Value = "in_out_sine")]
        InOutSine = 5,
        [EnumMember(Value = "in_quad")]
        InQuad = 6,
        [EnumMember(Value = "out_quad")]
        OutQuad = 7,
        [EnumMember(Value = "in_out_quad")]
        InOutQuad = 8,
        [EnumMember(Value = "in_cubic")]
        InCubic = 9,
        [EnumMember(Value = "out_cubic")]
        OutCubic = 10,
        [EnumMember(Value = "in_out_cubic")]
        InOutCubic = 11,
    }
    
    public enum eWaypointLoopMode
    {
        [EnumMember(Value = "once")]
        Once = 0,
        [EnumMember(Value = "loop")]
        Loop = 1,
        [EnumMember(Value = "ping_pong")]
        PingPong = 2,
    }
    
    public enum eKinematicCurveType
    {
        [EnumMember(Value = "catmull_rom")]
        CatmullRom = 0,
        [EnumMember(Value = "b_spline")]
        BSpline = 1,
        [EnumMember(Value = "bezier")]
        Bezier = 2,
    }
    
    public enum eOrbitPlaneMode
    {
        [EnumMember(Value = "xy")]
        XY = 0,
        [EnumMember(Value = "xz")]
        XZ = 1,
        [EnumMember(Value = "yz")]
        YZ = 2,
        [EnumMember(Value = "custom")]
        Custom = 3,
    }
    
    public enum eOrbitCenterMode
    {
        [EnumMember(Value = "fixed_point")]
        FixedPoint = 0,
        [EnumMember(Value = "follow_target")]
        FollowTarget = 1,
    }
    
    public enum eOrbitRadiusMode
    {
        [EnumMember(Value = "circle")]
        Circle = 0,
        [EnumMember(Value = "ellipse")]
        Ellipse = 1,
    }
    
    public enum eOrbitOrientationMode
    {
        [EnumMember(Value = "keep_rotation")]
        KeepRotation = 0,
        [EnumMember(Value = "face_center")]
        FaceCenter = 1,
        [EnumMember(Value = "orient_along_velocity")]
        OrientAlongVelocity = 2,
    }
    
    public enum eOrbitEndMode
    {
        [EnumMember(Value = "loop")]
        Loop = 0,
        [EnumMember(Value = "ping_pong")]
        PingPong = 1,
        [EnumMember(Value = "clamp")]
        Clamp = 2,
    }
    
    public enum eFollowOffsetSpace
    {
        [EnumMember(Value = "target_local")]
        TargetLocal = 0,
        [EnumMember(Value = "world")]
        World = 1,
    }
    
    public enum eFollowRotationMode
    {
        [EnumMember(Value = "keep_world_rotation")]
        KeepWorldRotation = 0,
        [EnumMember(Value = "match_target_rotation")]
        MatchTargetRotation = 1,
        [EnumMember(Value = "look_at_target")]
        LookAtTarget = 2,
        [EnumMember(Value = "orient_along_velocity")]
        OrientAlongVelocity = 3,
    }
    
    public enum eProjectileKind
    {
        [EnumMember(Value = "dyn_sim")]
        DynSim = 0,
        [EnumMember(Value = "analytic")]
        Analytic = 1,
        [EnumMember(Value = "hitscan")]
        Hitscan = 2,
    }
    
    public enum eProjectileMotionModel
    {
        [EnumMember(Value = "linear")]
        Linear = 0,
        [EnumMember(Value = "ballistic")]
        Ballistic = 1,
        [EnumMember(Value = "homing_steer")]
        HomingSteer = 2,
        [EnumMember(Value = "homing_lead")]
        HomingLead = 3,
        [EnumMember(Value = "homing_pn")]
        HomingPn = 4,
    }
    
    public enum eProjectileHitModel
    {
        [EnumMember(Value = "raycast_fallback")]
        RaycastFallback = 0,
        [EnumMember(Value = "shape_sweep")]
        ShapeSweep = 1,
        [EnumMember(Value = "sphere_sweep")]
        SphereSweep = 2,
        [EnumMember(Value = "expanding_shape_sweep")]
        ExpandingShapeSweep = 3,
        [EnumMember(Value = "expanding_sphere_sweep")]
        ExpandingSphereSweep = 4,
    }

} // namespace JamUnity.Authoring.Physics
