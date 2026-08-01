using System;
using System.Collections.Generic;
using UnityEngine;
using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Physics
{
    internal static class PhysicsGeneratedDtoAdapter
    {
        public static List<float> ToList(Vector3 value)
        {
            return new List<float> { value.x, value.y, value.z };
        }

        public static List<float> ToList(Quaternion value)
        {
            return new List<float> { value.x, value.y, value.z, value.w };
        }

        public static Vector3 ToVector3(IReadOnlyList<float> value)
        {
            if (value == null || value.Count < 3)
                return Vector3.zero;

            return new Vector3(value[0], value[1], value[2]);
        }

        public static Quaternion ToQuaternion(IReadOnlyList<float> value)
        {
            if (value == null || value.Count < 4)
                return Quaternion.identity;

            return new Quaternion(value[0], value[1], value[2], value[3]);
        }

        public static SharedGen.TransformDto ToTransformDto(Vector3 position, Quaternion rotation)
        {
            return new SharedGen.TransformDto
            {
                p = ToList(position),
                q = ToList(rotation),
            };
        }

        public static string ToHandleName(IAssetData asset)
        {
            return asset?.AssetName?.Trim() ?? string.Empty;
        }

        public static string ToBodyTypeText(eBodyType value)
        {
            return value switch
            {
                eBodyType.CharacterBody => "character_body",
                _ => "rigid_body",
            };
        }

        public static eKinematicSourceType ToRuntimeSourceType(string value)
        {
            return value switch
            {
                "waypoint" => eKinematicSourceType.Waypoint,
                "curve" => eKinematicSourceType.Curve,
                "orbit" => eKinematicSourceType.Orbit,
                "follow" => eKinematicSourceType.Follow,
                _ => eKinematicSourceType.NetworkPose,
            };
        }

        public static string ToGeneratedSourceType(eKinematicSourceType value)
        {
            return value switch
            {
                eKinematicSourceType.Waypoint => "waypoint",
                eKinematicSourceType.Curve => "curve",
                eKinematicSourceType.Orbit => "orbit",
                eKinematicSourceType.Follow => "follow",
                _ => "network_pose",
            };
        }

        public static SharedGen.SimFilterDto ToSimFilterDto(SimulationFilterData value)
        {
            value ??= new SimulationFilterData();
            return new SharedGen.SimFilterDto
            {
                word0 = value.Word0,
                word1 = value.Word1,
                word2 = value.Word2,
                word3 = value.Word3,
            };
        }

        public static SharedGen.QueryFilterDto ToQueryFilterDto(ShapeQueryFilterData value)
        {
            value ??= new ShapeQueryFilterData();
            return new SharedGen.QueryFilterDto
            {
                word0 = value.Word0,
                word1 = value.Word1,
                word2 = value.Word2,
                word3 = value.Word3,
            };
        }

        public static SharedGen.QueryFilterDto ToQueryFilterDto(FilterData value)
        {
            value ??= new FilterData();
            return new SharedGen.QueryFilterDto
            {
                word0 = value.word0,
                word1 = value.word1,
                word2 = value.word2,
                word3 = value.word3,
            };
        }

        public static FilterData ToFilterData(SharedGen.SimFilterDto value)
        {
            if (value == null)
                return default;

            return new FilterData { word0 = value.word0, word1 = value.word1, word2 = value.word2, word3 = value.word3 };
        }

        public static FilterData ToFilterData(SharedGen.QueryFilterDto value)
        {
            if (value == null)
                return default;

            return new FilterData { word0 = value.word0, word1 = value.word1, word2 = value.word2, word3 = value.word3 };
        }

        public static SimulationFilterData ToSimulationFilterData(SharedGen.SimFilterDto value)
        {
            return value == null
                ? new SimulationFilterData()
                : SimulationFilterData.FromWords(value.word0, value.word1, value.word2, value.word3);
        }

        public static ShapeQueryFilterData ToShapeQueryFilterData(SharedGen.QueryFilterDto value)
        {
            return value == null
                ? new ShapeQueryFilterData()
                : ShapeQueryFilterData.FromWords(value.word0, value.word1, value.word2, value.word3);
        }

        public static SharedGen.eMeshDtoType ToGeneratedMeshType(eMeshType value)
        {
            return value switch
            {
                eMeshType.Convex => SharedGen.eMeshDtoType.Convex,
                _ => SharedGen.eMeshDtoType.Triangle,
            };
        }

        public static eMeshType ToRuntimeMeshType(SharedGen.eMeshDtoType value)
        {
            return value switch
            {
                SharedGen.eMeshDtoType.Convex => eMeshType.Convex,
                _ => eMeshType.Triangle,
            };
        }

        public static SharedGen.eCctBodyDtoPolicy ToGeneratedCctPolicy(eCCTPolicy value)
        {
            return SharedGen.eCctBodyDtoPolicy.Default;
        }

        public static eCCTPolicy ToRuntimeCctPolicy(SharedGen.eCctBodyDtoPolicy value)
        {
            return eCCTPolicy.Default;
        }

        public static SharedGen.eShapeDtoType ToGeneratedShapeType(eShapeType value)
        {
            return value switch
            {
                eShapeType.Box => SharedGen.eShapeDtoType.Box,
                eShapeType.Sphere => SharedGen.eShapeDtoType.Sphere,
                eShapeType.Capsule => SharedGen.eShapeDtoType.Capsule,
                eShapeType.Plane => SharedGen.eShapeDtoType.Plane,
                eShapeType.TriangleMesh => SharedGen.eShapeDtoType.TriangleMesh,
                eShapeType.ConvexMesh => SharedGen.eShapeDtoType.ConvexMesh,
                _ => SharedGen.eShapeDtoType.None,
            };
        }

        public static eShapeType ToRuntimeShapeType(SharedGen.eShapeDtoType value)
        {
            return value switch
            {
                SharedGen.eShapeDtoType.Box => eShapeType.Box,
                SharedGen.eShapeDtoType.Sphere => eShapeType.Sphere,
                SharedGen.eShapeDtoType.Capsule => eShapeType.Capsule,
                SharedGen.eShapeDtoType.Plane => eShapeType.Plane,
                SharedGen.eShapeDtoType.TriangleMesh => eShapeType.TriangleMesh,
                SharedGen.eShapeDtoType.ConvexMesh => eShapeType.ConvexMesh,
                _ => eShapeType.None,
            };
        }

        public static SharedGen.eShapeDtoShapeFlag ToGeneratedShapeFlag(eShapeFlag value)
        {
            return value switch
            {
                eShapeFlag.SimulationOnly => SharedGen.eShapeDtoShapeFlag.SimulationOnly,
                eShapeFlag.Trigger => SharedGen.eShapeDtoShapeFlag.Trigger,
                eShapeFlag.TriggerOnly => SharedGen.eShapeDtoShapeFlag.TriggerOnly,
                eShapeFlag.QueryOnly => SharedGen.eShapeDtoShapeFlag.QueryOnly,
                _ => SharedGen.eShapeDtoShapeFlag.Simulation,
            };
        }

        public static eShapeFlag ToRuntimeShapeFlag(SharedGen.eShapeDtoShapeFlag value)
        {
            return value switch
            {
                SharedGen.eShapeDtoShapeFlag.SimulationOnly => eShapeFlag.SimulationOnly,
                SharedGen.eShapeDtoShapeFlag.Trigger => eShapeFlag.Trigger,
                SharedGen.eShapeDtoShapeFlag.TriggerOnly => eShapeFlag.TriggerOnly,
                SharedGen.eShapeDtoShapeFlag.QueryOnly => eShapeFlag.QueryOnly,
                _ => eShapeFlag.Simulation,
            };
        }

        public static SharedGen.StanceConfigDto ToGeneratedStanceDto(CharacterMoveConfigData.StanceConfig value)
        {
            return new SharedGen.StanceConfigDto
            {
                standingHeight = value.standingHeight,
                crouchHeight = value.crouchHeight,
                crouchSpeedMultiplier = value.crouchSpeedMultiplier,
                holdToCrouch = value.holdToCrouch,
                proneHeight = value.proneHeight,
                proneSpeedMultiplier = value.proneSpeedMultiplier,
                holdToProne = value.holdToProne,
            };
        }

        public static CharacterMoveConfigData.StanceConfig ToRuntimeStanceConfig(SharedGen.StanceConfigDto value)
        {
            if (value == null)
                return default;

            return new CharacterMoveConfigData.StanceConfig
            {
                standingHeight = value.standingHeight,
                crouchHeight = value.crouchHeight,
                crouchSpeedMultiplier = value.crouchSpeedMultiplier,
                holdToCrouch = value.holdToCrouch,
                proneHeight = value.proneHeight,
                proneSpeedMultiplier = value.proneSpeedMultiplier,
                holdToProne = value.holdToProne,
            };
        }

        public static SharedGen.GaitConfigDto ToGeneratedGaitDto(CharacterMoveConfigData.GaitConfig value)
        {
            return new SharedGen.GaitConfigDto
            {
                walkSpeedMultiplier = value.walkSpeedMultiplier,
                runSpeedMultiplier = value.runSpeedMultiplier,
                sprintSpeedMultiplier = value.sprintSpeedMultiplier,
                sprintAccelMultiplier = value.sprintAccelMultiplier,
                sprintMinSpeedToStart = value.sprintMinSpeedToStart,
                sprintAllowInAir = value.sprintAllowInAir,
            };
        }

        public static CharacterMoveConfigData.GaitConfig ToRuntimeGaitConfig(SharedGen.GaitConfigDto value)
        {
            if (value == null)
                return default;

            return new CharacterMoveConfigData.GaitConfig
            {
                walkSpeedMultiplier = value.walkSpeedMultiplier,
                runSpeedMultiplier = value.runSpeedMultiplier,
                sprintSpeedMultiplier = value.sprintSpeedMultiplier,
                sprintAccelMultiplier = value.sprintAccelMultiplier,
                sprintMinSpeedToStart = value.sprintMinSpeedToStart,
                sprintAllowInAir = value.sprintAllowInAir,
            };
        }

        public static SharedGen.JumpConfigDto ToGeneratedJumpDto(CharacterMoveConfigData.JumpConfig value)
        {
            return new SharedGen.JumpConfigDto
            {
                speed = value.speed,
                coyoteTime = value.coyoteTime,
                jumpBuffer = value.jumpBuffer,
                edgeTrigger = value.edgeTrigger,
            };
        }

        public static CharacterMoveConfigData.JumpConfig ToRuntimeJumpConfig(SharedGen.JumpConfigDto value)
        {
            if (value == null)
                return default;

            return new CharacterMoveConfigData.JumpConfig
            {
                speed = value.speed,
                coyoteTime = value.coyoteTime,
                jumpBuffer = value.jumpBuffer,
                edgeTrigger = value.edgeTrigger,
            };
        }

        public static SharedGen.DashConfigDto ToGeneratedDashDto(CharacterMoveConfigData.DashConfig value)
        {
            return new SharedGen.DashConfigDto
            {
                speed = value.speed,
                duration = value.duration,
                overrideLocomotion = value.overrideLocomotion,
                allowInAir = value.allowInAir,
                endOnCollision = value.endOnCollision,
                steerFactor = value.steerFactor,
            };
        }

        public static CharacterMoveConfigData.DashConfig ToRuntimeDashConfig(SharedGen.DashConfigDto value)
        {
            if (value == null)
                return default;

            return new CharacterMoveConfigData.DashConfig
            {
                speed = value.speed,
                duration = value.duration,
                overrideLocomotion = value.overrideLocomotion,
                allowInAir = value.allowInAir,
                endOnCollision = value.endOnCollision,
                steerFactor = value.steerFactor,
            };
        }

        public static SharedGen.eWaypointSourceDtoLoopMode ToGeneratedWaypointLoopMode(eWaypointLoopMode value)
        {
            return value switch
            {
                eWaypointLoopMode.Loop => SharedGen.eWaypointSourceDtoLoopMode.Loop,
                eWaypointLoopMode.PingPong => SharedGen.eWaypointSourceDtoLoopMode.PingPong,
                _ => SharedGen.eWaypointSourceDtoLoopMode.Once,
            };
        }

        public static eWaypointLoopMode ToRuntimeWaypointLoopMode(SharedGen.eWaypointSourceDtoLoopMode value)
        {
            return value switch
            {
                SharedGen.eWaypointSourceDtoLoopMode.Loop => eWaypointLoopMode.Loop,
                SharedGen.eWaypointSourceDtoLoopMode.PingPong => eWaypointLoopMode.PingPong,
                _ => eWaypointLoopMode.Once,
            };
        }

        public static SharedGen.eWaypointSourceDtoEaseType ToGeneratedWaypointEaseType(eKinematicEaseType value)
        {
            return (SharedGen.eWaypointSourceDtoEaseType)(int)value;
        }

        public static SharedGen.eCurveSourceDtoEaseType ToGeneratedCurveEaseType(eKinematicEaseType value)
        {
            return (SharedGen.eCurveSourceDtoEaseType)(int)value;
        }

        public static eKinematicEaseType ToRuntimeEaseType(SharedGen.eWaypointSourceDtoEaseType value)
        {
            return (eKinematicEaseType)(int)value;
        }

        public static eKinematicEaseType ToRuntimeEaseType(SharedGen.eCurveSourceDtoEaseType value)
        {
            return (eKinematicEaseType)(int)value;
        }

        public static SharedGen.eCurveSourceDtoType ToGeneratedCurveType(eKinematicCurveType value)
        {
            return value switch
            {
                eKinematicCurveType.BSpline => SharedGen.eCurveSourceDtoType.BSpline,
                eKinematicCurveType.Bezier => SharedGen.eCurveSourceDtoType.Bezier,
                _ => SharedGen.eCurveSourceDtoType.CatmullRom,
            };
        }

        public static eKinematicCurveType ToRuntimeCurveType(SharedGen.eCurveSourceDtoType value)
        {
            return value switch
            {
                SharedGen.eCurveSourceDtoType.BSpline => eKinematicCurveType.BSpline,
                SharedGen.eCurveSourceDtoType.Bezier => eKinematicCurveType.Bezier,
                _ => eKinematicCurveType.CatmullRom,
            };
        }

        public static SharedGen.eOrbitSourceDtoCenterMode ToGeneratedOrbitCenterMode(eOrbitCenterMode value)
        {
            return value switch
            {
                eOrbitCenterMode.FollowTarget => SharedGen.eOrbitSourceDtoCenterMode.FollowTarget,
                _ => SharedGen.eOrbitSourceDtoCenterMode.FixedPoint,
            };
        }

        public static eOrbitCenterMode ToRuntimeOrbitCenterMode(SharedGen.eOrbitSourceDtoCenterMode value)
        {
            return value switch
            {
                SharedGen.eOrbitSourceDtoCenterMode.FollowTarget => eOrbitCenterMode.FollowTarget,
                _ => eOrbitCenterMode.FixedPoint,
            };
        }

        public static SharedGen.eOrbitSourceDtoPlaneMode ToGeneratedOrbitPlaneMode(eOrbitPlaneMode value)
        {
            return value switch
            {
                eOrbitPlaneMode.XY => SharedGen.eOrbitSourceDtoPlaneMode.Xy,
                eOrbitPlaneMode.YZ => SharedGen.eOrbitSourceDtoPlaneMode.Yz,
                eOrbitPlaneMode.Custom => SharedGen.eOrbitSourceDtoPlaneMode.Custom,
                _ => SharedGen.eOrbitSourceDtoPlaneMode.Xz,
            };
        }

        public static eOrbitPlaneMode ToRuntimeOrbitPlaneMode(SharedGen.eOrbitSourceDtoPlaneMode value)
        {
            return value switch
            {
                SharedGen.eOrbitSourceDtoPlaneMode.Xy => eOrbitPlaneMode.XY,
                SharedGen.eOrbitSourceDtoPlaneMode.Yz => eOrbitPlaneMode.YZ,
                SharedGen.eOrbitSourceDtoPlaneMode.Custom => eOrbitPlaneMode.Custom,
                _ => eOrbitPlaneMode.XZ,
            };
        }

        public static SharedGen.eOrbitSourceDtoRadiusMode ToGeneratedOrbitRadiusMode(eOrbitRadiusMode value)
        {
            return value switch
            {
                eOrbitRadiusMode.Ellipse => SharedGen.eOrbitSourceDtoRadiusMode.Ellipse,
                _ => SharedGen.eOrbitSourceDtoRadiusMode.Circle,
            };
        }

        public static eOrbitRadiusMode ToRuntimeOrbitRadiusMode(SharedGen.eOrbitSourceDtoRadiusMode value)
        {
            return value switch
            {
                SharedGen.eOrbitSourceDtoRadiusMode.Ellipse => eOrbitRadiusMode.Ellipse,
                _ => eOrbitRadiusMode.Circle,
            };
        }

        public static SharedGen.eOrbitSourceDtoEndMode ToGeneratedOrbitEndMode(eOrbitEndMode value)
        {
            return value switch
            {
                eOrbitEndMode.PingPong => SharedGen.eOrbitSourceDtoEndMode.PingPong,
                eOrbitEndMode.Clamp => SharedGen.eOrbitSourceDtoEndMode.Clamp,
                _ => SharedGen.eOrbitSourceDtoEndMode.Loop,
            };
        }

        public static eOrbitEndMode ToRuntimeOrbitEndMode(SharedGen.eOrbitSourceDtoEndMode value)
        {
            return value switch
            {
                SharedGen.eOrbitSourceDtoEndMode.PingPong => eOrbitEndMode.PingPong,
                SharedGen.eOrbitSourceDtoEndMode.Clamp => eOrbitEndMode.Clamp,
                _ => eOrbitEndMode.Loop,
            };
        }

        public static SharedGen.eOrbitSourceDtoOrientationMode ToGeneratedOrbitOrientationMode(eOrbitOrientationMode value)
        {
            return value switch
            {
                eOrbitOrientationMode.FaceCenter => SharedGen.eOrbitSourceDtoOrientationMode.FaceCenter,
                eOrbitOrientationMode.OrientAlongVelocity => SharedGen.eOrbitSourceDtoOrientationMode.OrientAlongVelocity,
                _ => SharedGen.eOrbitSourceDtoOrientationMode.KeepRotation,
            };
        }

        public static eOrbitOrientationMode ToRuntimeOrbitOrientationMode(SharedGen.eOrbitSourceDtoOrientationMode value)
        {
            return value switch
            {
                SharedGen.eOrbitSourceDtoOrientationMode.FaceCenter => eOrbitOrientationMode.FaceCenter,
                SharedGen.eOrbitSourceDtoOrientationMode.OrientAlongVelocity => eOrbitOrientationMode.OrientAlongVelocity,
                _ => eOrbitOrientationMode.KeepRotation,
            };
        }

        public static SharedGen.eFollowSourceDtoOffsetSpace ToGeneratedFollowOffsetSpace(eFollowOffsetSpace value)
        {
            return value switch
            {
                eFollowOffsetSpace.World => SharedGen.eFollowSourceDtoOffsetSpace.World,
                _ => SharedGen.eFollowSourceDtoOffsetSpace.TargetLocal,
            };
        }

        public static eFollowOffsetSpace ToRuntimeFollowOffsetSpace(SharedGen.eFollowSourceDtoOffsetSpace value)
        {
            return value switch
            {
                SharedGen.eFollowSourceDtoOffsetSpace.World => eFollowOffsetSpace.World,
                _ => eFollowOffsetSpace.TargetLocal,
            };
        }

        public static SharedGen.eFollowSourceDtoRotationMode ToGeneratedFollowRotationMode(eFollowRotationMode value)
        {
            return value switch
            {
                eFollowRotationMode.MatchTargetRotation => SharedGen.eFollowSourceDtoRotationMode.MatchTargetRotation,
                eFollowRotationMode.LookAtTarget => SharedGen.eFollowSourceDtoRotationMode.LookAtTarget,
                eFollowRotationMode.OrientAlongVelocity => SharedGen.eFollowSourceDtoRotationMode.OrientAlongVelocity,
                _ => SharedGen.eFollowSourceDtoRotationMode.KeepWorldRotation,
            };
        }

        public static eFollowRotationMode ToRuntimeFollowRotationMode(SharedGen.eFollowSourceDtoRotationMode value)
        {
            return value switch
            {
                SharedGen.eFollowSourceDtoRotationMode.MatchTargetRotation => eFollowRotationMode.MatchTargetRotation,
                SharedGen.eFollowSourceDtoRotationMode.LookAtTarget => eFollowRotationMode.LookAtTarget,
                SharedGen.eFollowSourceDtoRotationMode.OrientAlongVelocity => eFollowRotationMode.OrientAlongVelocity,
                _ => eFollowRotationMode.KeepWorldRotation,
            };
        }

        public static SharedGen.eProjectileConfigDtoKind ToGeneratedProjectileKind(eProjectileKind value)
        {
            return value switch
            {
                eProjectileKind.Analytic => SharedGen.eProjectileConfigDtoKind.Analytic,
                eProjectileKind.Hitscan => SharedGen.eProjectileConfigDtoKind.Hitscan,
                _ => SharedGen.eProjectileConfigDtoKind.DynSim,
            };
        }

        public static eProjectileKind ToRuntimeProjectileKind(SharedGen.eProjectileConfigDtoKind value)
        {
            return value switch
            {
                SharedGen.eProjectileConfigDtoKind.Analytic => eProjectileKind.Analytic,
                SharedGen.eProjectileConfigDtoKind.Hitscan => eProjectileKind.Hitscan,
                _ => eProjectileKind.DynSim,
            };
        }

        public static SharedGen.eProjectileMotionConfigDtoModel ToGeneratedProjectileMotionModel(eProjectileMotionModel value)
        {
            return value switch
            {
                eProjectileMotionModel.Ballistic => SharedGen.eProjectileMotionConfigDtoModel.Ballistic,
                eProjectileMotionModel.HomingSteer => SharedGen.eProjectileMotionConfigDtoModel.HomingSteer,
                eProjectileMotionModel.HomingLead => SharedGen.eProjectileMotionConfigDtoModel.HomingLead,
                eProjectileMotionModel.HomingPn => SharedGen.eProjectileMotionConfigDtoModel.HomingPn,
                _ => SharedGen.eProjectileMotionConfigDtoModel.Linear,
            };
        }

        public static eProjectileMotionModel ToRuntimeProjectileMotionModel(SharedGen.eProjectileMotionConfigDtoModel value)
        {
            return value switch
            {
                SharedGen.eProjectileMotionConfigDtoModel.Ballistic => eProjectileMotionModel.Ballistic,
                SharedGen.eProjectileMotionConfigDtoModel.HomingSteer => eProjectileMotionModel.HomingSteer,
                SharedGen.eProjectileMotionConfigDtoModel.HomingLead => eProjectileMotionModel.HomingLead,
                SharedGen.eProjectileMotionConfigDtoModel.HomingPn => eProjectileMotionModel.HomingPn,
                _ => eProjectileMotionModel.Linear,
            };
        }

        public static SharedGen.eProjectileHitConfigDtoModel ToGeneratedProjectileHitModel(eProjectileHitModel value)
        {
            return value switch
            {
                eProjectileHitModel.ShapeSweep => SharedGen.eProjectileHitConfigDtoModel.ShapeSweep,
                eProjectileHitModel.SphereSweep => SharedGen.eProjectileHitConfigDtoModel.SphereSweep,
                eProjectileHitModel.ExpandingShapeSweep => SharedGen.eProjectileHitConfigDtoModel.ExpandingShapeSweep,
                eProjectileHitModel.ExpandingSphereSweep => SharedGen.eProjectileHitConfigDtoModel.ExpandingSphereSweep,
                _ => SharedGen.eProjectileHitConfigDtoModel.RaycastFallback,
            };
        }

        public static eProjectileHitModel ToRuntimeProjectileHitModel(SharedGen.eProjectileHitConfigDtoModel value)
        {
            return value switch
            {
                SharedGen.eProjectileHitConfigDtoModel.ShapeSweep => eProjectileHitModel.ShapeSweep,
                SharedGen.eProjectileHitConfigDtoModel.SphereSweep => eProjectileHitModel.SphereSweep,
                SharedGen.eProjectileHitConfigDtoModel.ExpandingShapeSweep => eProjectileHitModel.ExpandingShapeSweep,
                SharedGen.eProjectileHitConfigDtoModel.ExpandingSphereSweep => eProjectileHitModel.ExpandingSphereSweep,
                _ => eProjectileHitModel.RaycastFallback,
            };
        }

        public static SharedGen.eRigidBehaviorDtoKind ToGeneratedRigidBehaviorKind(eRigidBehaviorKind value)
        {
            return value switch
            {
                eRigidBehaviorKind.KinematicDriver => SharedGen.eRigidBehaviorDtoKind.KinematicDriver,
                eRigidBehaviorKind.Projectile => SharedGen.eRigidBehaviorDtoKind.Projectile,
                _ => SharedGen.eRigidBehaviorDtoKind.None,
            };
        }

        public static eRigidBehaviorKind ToRuntimeRigidBehaviorKind(SharedGen.eRigidBehaviorDtoKind value)
        {
            return value switch
            {
                SharedGen.eRigidBehaviorDtoKind.KinematicDriver => eRigidBehaviorKind.KinematicDriver,
                SharedGen.eRigidBehaviorDtoKind.Projectile => eRigidBehaviorKind.Projectile,
                _ => eRigidBehaviorKind.None,
            };
        }

        public static SharedGen.eCharacterBodyDtoControllerType ToGeneratedControllerType(eControllerType value)
        {
            return value switch
            {
                eControllerType.Player => SharedGen.eCharacterBodyDtoControllerType.Player,
                eControllerType.AI => SharedGen.eCharacterBodyDtoControllerType.Ai,
                _ => SharedGen.eCharacterBodyDtoControllerType.None,
            };
        }

        public static eControllerType ToRuntimeControllerType(SharedGen.eCharacterBodyDtoControllerType value)
        {
            return value switch
            {
                SharedGen.eCharacterBodyDtoControllerType.Player => eControllerType.Player,
                SharedGen.eCharacterBodyDtoControllerType.Ai => eControllerType.AI,
                _ => eControllerType.None,
            };
        }

        public static SharedGen.eCharacterPhysicsArchetypeDtoActorType ToGeneratedCharacterActorType(eActorType value)
        {
            return value switch
            {
                eActorType.Character => SharedGen.eCharacterPhysicsArchetypeDtoActorType.Character,
                eActorType.Projectile => SharedGen.eCharacterPhysicsArchetypeDtoActorType.Projectile,
                _ => SharedGen.eCharacterPhysicsArchetypeDtoActorType.Generic,
            };
        }

        public static SharedGen.eRigidPhysicsArchetypeDtoActorType ToGeneratedRigidActorType(eActorType value)
        {
            return value switch
            {
                eActorType.Character => SharedGen.eRigidPhysicsArchetypeDtoActorType.Character,
                eActorType.Projectile => SharedGen.eRigidPhysicsArchetypeDtoActorType.Projectile,
                _ => SharedGen.eRigidPhysicsArchetypeDtoActorType.Generic,
            };
        }

        public static eActorType ToRuntimeActorType(SharedGen.eCharacterPhysicsArchetypeDtoActorType value)
        {
            return value switch
            {
                SharedGen.eCharacterPhysicsArchetypeDtoActorType.Character => eActorType.Character,
                SharedGen.eCharacterPhysicsArchetypeDtoActorType.Projectile => eActorType.Projectile,
                _ => eActorType.Generic,
            };
        }

        public static eActorType ToRuntimeActorType(SharedGen.eRigidPhysicsArchetypeDtoActorType value)
        {
            return value switch
            {
                SharedGen.eRigidPhysicsArchetypeDtoActorType.Character => eActorType.Character,
                SharedGen.eRigidPhysicsArchetypeDtoActorType.Projectile => eActorType.Projectile,
                _ => eActorType.Generic,
            };
        }

        public static SharedGen.eCharacterPhysicsArchetypeDtoMotionType ToGeneratedCharacterMotionType(eMotionType value)
        {
            return (SharedGen.eCharacterPhysicsArchetypeDtoMotionType)(int)value;
        }

        public static SharedGen.eRigidPhysicsArchetypeDtoMotionType ToGeneratedRigidMotionType(eMotionType value)
        {
            return (SharedGen.eRigidPhysicsArchetypeDtoMotionType)(int)value;
        }

        public static eMotionType ToRuntimeMotionType(SharedGen.eCharacterPhysicsArchetypeDtoMotionType value)
        {
            return (eMotionType)(int)value;
        }

        public static eMotionType ToRuntimeMotionType(SharedGen.eRigidPhysicsArchetypeDtoMotionType value)
        {
            return (eMotionType)(int)value;
        }

        public static List<SharedGen.eCharacterPhysicsArchetypeDtoMotionFlags> ToGeneratedCharacterMotionFlags(eMotionFlag flags)
        {
            var result = new List<SharedGen.eCharacterPhysicsArchetypeDtoMotionFlags>();
            foreach (eMotionFlag value in Enum.GetValues(typeof(eMotionFlag)))
            {
                if (value == eMotionFlag.None || (flags & value) != value)
                    continue;

                result.Add((SharedGen.eCharacterPhysicsArchetypeDtoMotionFlags)Enum.Parse(typeof(SharedGen.eCharacterPhysicsArchetypeDtoMotionFlags), value.ToString()));
            }
            return result;
        }

        public static List<SharedGen.eRigidPhysicsArchetypeDtoMotionFlags> ToGeneratedRigidMotionFlags(eMotionFlag flags)
        {
            var result = new List<SharedGen.eRigidPhysicsArchetypeDtoMotionFlags>();
            foreach (eMotionFlag value in Enum.GetValues(typeof(eMotionFlag)))
            {
                if (value == eMotionFlag.None || (flags & value) != value)
                    continue;

                result.Add((SharedGen.eRigidPhysicsArchetypeDtoMotionFlags)Enum.Parse(typeof(SharedGen.eRigidPhysicsArchetypeDtoMotionFlags), value.ToString()));
            }
            return result;
        }

        public static eMotionFlag ToRuntimeMotionFlags(IReadOnlyList<SharedGen.eCharacterPhysicsArchetypeDtoMotionFlags> flags)
        {
            eMotionFlag result = eMotionFlag.None;
            if (flags == null)
                return result;

            for (int i = 0; i < flags.Count; ++i)
                result |= (eMotionFlag)Enum.Parse(typeof(eMotionFlag), flags[i].ToString());
            return result;
        }

        public static eMotionFlag ToRuntimeMotionFlags(IReadOnlyList<SharedGen.eRigidPhysicsArchetypeDtoMotionFlags> flags)
        {
            eMotionFlag result = eMotionFlag.None;
            if (flags == null)
                return result;

            for (int i = 0; i < flags.Count; ++i)
                result |= (eMotionFlag)Enum.Parse(typeof(eMotionFlag), flags[i].ToString());
            return result;
        }
    }
} // namespace JamUnity.Authoring.Physics
