using System;
using System.Collections.Generic;
using UnityEngine;

using JamUnity.Core.Data;
using SharedGen = JamUnity.SharedData.Generated;

namespace JamUnity.Authoring.Physics
{
    [CreateAssetMenu(menuName = "JamUnity/Physics/Kinematic Driver Config Data", fileName = "KinematicDriverConfigData")]
    public sealed class KinematicDriverConfigData : AssetData<SharedGen.KinematicDriverConfigDto>
    {
        [Serializable]
        public struct Waypoint
        {
            public Vector3 p;
        }

        [Serializable]
        public struct OrbitConfig
        {
            public Vector3 center;
            public Vector3 axis;
            public float   radius;
        }

        [Serializable]
        public struct FollowConfig
        {
            public Vector3 offset;
        }

        [SerializeField] private bool                 computeDerivedVel = true;
        [SerializeField] private bool                 carryRiders = true;
        [SerializeField] private bool                 sweep = true;
        [SerializeField] private float                maxSpeed = 10f;
        [SerializeField] private eKinematicSourceType sourceType;
        [SerializeField] private float                speed = 1f;
        [SerializeField] private float                duration = 1f;
        [SerializeField] private bool                 loop;
        [SerializeField] private bool                 buildSegments;
        [SerializeField] private bool                 useEaseProfile;
        [SerializeField] private eKinematicEaseType   easeType;
        [SerializeField] private float                alpha = 1f;
        [SerializeField] private eKinematicCurveType  curveType;
        [SerializeField] private List<Waypoint>       waypoints = new();
        [SerializeField] private List<Vector3>        controlPoints = new();
        [SerializeField] private OrbitConfig          orbit;
        [SerializeField] private FollowConfig         follow;
        [SerializeField] private bool                 networkPoseComputeDerivedVelocity = true;

        public IReadOnlyList<Waypoint> Waypoints     => waypoints;
        public IReadOnlyList<Vector3>  ControlPoints => controlPoints;

        public override SharedGen.KinematicDriverConfigDto ToDto()
        {
            var source = CreateGeneratedSourceDto();
            return new SharedGen.KinematicDriverConfigDto
            {
                common = new SharedGen.KinematicCommonDto
                {
                    computeDerivedVel   = computeDerivedVel,
                    carryRiders         = carryRiders,
                    sweep               = sweep,
                    maxSpeed            = maxSpeed,
                },
                source = source,
            };
        }

        public override void FromDto(SharedGen.KinematicDriverConfigDto dto)
        {
            if (dto == null)
                return;

            if (dto.common != null)
            {
                computeDerivedVel = dto.common.computeDerivedVel;
                carryRiders       = dto.common.carryRiders;
                sweep             = dto.common.sweep;
                maxSpeed          = dto.common.maxSpeed;
            }

            waypoints.Clear();
            controlPoints.Clear();

            if (dto.source != null)
                ApplyGeneratedSourceDto(dto.source);
        }
        
        private SharedGen.KinematicSourceDto CreateGeneratedSourceDto()
        {
            switch (sourceType)
            {
                case eKinematicSourceType.Waypoint:
                {
                    var points = new List<SharedGen.KinematicWaypointDto>(waypoints.Count);
                    for (int i = 0; i < waypoints.Count; ++i)
                    {
                        points.Add(new SharedGen.KinematicWaypointDto
                        {
                            pose = PhysicsGeneratedDtoAdapter.ToTransformDto(waypoints[i].p, Quaternion.identity),
                            pauseDuration = 0f,
                        });
                    }

                    return new SharedGen.WaypointSourceDto
                    {
                        sourceType      = PhysicsGeneratedDtoAdapter.ToGeneratedSourceType(sourceType),
                        waypoints       = points,
                        speed           = speed,
                        loopMode        = PhysicsGeneratedDtoAdapter.ToGeneratedWaypointLoopMode(loop ? eWaypointLoopMode.Loop : eWaypointLoopMode.Once),
                        useEaseProfile  = useEaseProfile,
                        easeType        = PhysicsGeneratedDtoAdapter.ToGeneratedWaypointEaseType(easeType),
                    };
                }

                case eKinematicSourceType.Curve:
                {
                    var points = new List<List<float>>(controlPoints.Count);
                    for (int i = 0; i < controlPoints.Count; ++i)
                        points.Add(PhysicsGeneratedDtoAdapter.ToList(controlPoints[i]));

                    return new SharedGen.CurveSourceDto
                    {
                        sourceType      = PhysicsGeneratedDtoAdapter.ToGeneratedSourceType(sourceType),
                        controlPoints   = points,
                        type            = PhysicsGeneratedDtoAdapter.ToGeneratedCurveType(curveType),
                        speed           = speed,
                        duration        = duration,
                        loop            = loop,
                        buildSegments   = buildSegments ? 64u : 0u,
                        useEaseProfile  = useEaseProfile,
                        easeType        = PhysicsGeneratedDtoAdapter.ToGeneratedCurveEaseType(easeType),
                        alpha           = alpha,
                        degree          = 3u,
                    };
                }

                case eKinematicSourceType.Orbit:
                    return new SharedGen.OrbitSourceDto
                    {
                        sourceType              = PhysicsGeneratedDtoAdapter.ToGeneratedSourceType(sourceType),
                        centerMode              = PhysicsGeneratedDtoAdapter.ToGeneratedOrbitCenterMode(eOrbitCenterMode.FixedPoint),
                        fixedCenter             = PhysicsGeneratedDtoAdapter.ToList(orbit.center),
                        planeMode               = PhysicsGeneratedDtoAdapter.ToGeneratedOrbitPlaneMode(eOrbitPlaneMode.Custom),
                        customPlaneNormal       = PhysicsGeneratedDtoAdapter.ToList(orbit.axis),
                        radiusMode              = PhysicsGeneratedDtoAdapter.ToGeneratedOrbitRadiusMode(eOrbitRadiusMode.Circle),
                        radius                  = orbit.radius,
                        endMode                 = PhysicsGeneratedDtoAdapter.ToGeneratedOrbitEndMode(loop ? eOrbitEndMode.Loop : eOrbitEndMode.Clamp),
                        orientationMode         = PhysicsGeneratedDtoAdapter.ToGeneratedOrbitOrientationMode(eOrbitOrientationMode.OrientAlongVelocity),
                        computeDerivedVelocity  = networkPoseComputeDerivedVelocity,
                    };

                case eKinematicSourceType.Follow:
                    return new SharedGen.FollowSourceDto
                    {
                        sourceType              = PhysicsGeneratedDtoAdapter.ToGeneratedSourceType(sourceType),
                        offset                  = PhysicsGeneratedDtoAdapter.ToList(follow.offset),
                        offsetSpace             = PhysicsGeneratedDtoAdapter.ToGeneratedFollowOffsetSpace(eFollowOffsetSpace.TargetLocal),
                        rotationMode            = PhysicsGeneratedDtoAdapter.ToGeneratedFollowRotationMode(eFollowRotationMode.KeepWorldRotation),
                        keepLastPoseIfMissing   = true,
                        computeDerivedVelocity  = networkPoseComputeDerivedVelocity,
                    };

                default:
                    return new SharedGen.NetworkPoseSourceDto
                    {
                        sourceType = PhysicsGeneratedDtoAdapter.ToGeneratedSourceType(eKinematicSourceType.NetworkPose),
                        computeDerivedVelocity = networkPoseComputeDerivedVelocity,
                    };
            }
        }

        private void ApplyGeneratedSourceDto(SharedGen.KinematicSourceDto source)
        {
            if (source == null)
                return;

            sourceType = PhysicsGeneratedDtoAdapter.ToRuntimeSourceType(source switch
            {
                SharedGen.WaypointSourceDto     waypoint    => waypoint.sourceType,
                SharedGen.CurveSourceDto        curve       => curve.sourceType,
                SharedGen.OrbitSourceDto        orbitDto    => orbitDto.sourceType,
                SharedGen.FollowSourceDto       followDto   => followDto.sourceType,
                SharedGen.NetworkPoseSourceDto  networkPose => networkPose.sourceType,
                _ => string.Empty,
            });

            switch (source)
            {
                case SharedGen.WaypointSourceDto waypointSource:
                    speed           = waypointSource.speed;
                    loop            = PhysicsGeneratedDtoAdapter.ToRuntimeWaypointLoopMode(waypointSource.loopMode) == eWaypointLoopMode.Loop;
                    useEaseProfile  = waypointSource.useEaseProfile;
                    easeType        = PhysicsGeneratedDtoAdapter.ToRuntimeEaseType(waypointSource.easeType);
                    if (waypointSource.waypoints != null)
                    {
                        for (int i = 0; i < waypointSource.waypoints.Count; ++i)
                            waypoints.Add(new Waypoint { p = PhysicsGeneratedDtoAdapter.ToVector3(waypointSource.waypoints[i].pose?.p) });
                    }
                    break;

                case SharedGen.CurveSourceDto curveSource:
                    speed           = curveSource.speed;
                    duration        = curveSource.duration;
                    loop            = curveSource.loop;
                    buildSegments   = curveSource.buildSegments != 0;
                    useEaseProfile  = curveSource.useEaseProfile;
                    easeType        = PhysicsGeneratedDtoAdapter.ToRuntimeEaseType(curveSource.easeType);
                    alpha           = curveSource.alpha;
                    curveType       = PhysicsGeneratedDtoAdapter.ToRuntimeCurveType(curveSource.type);
                    if (curveSource.controlPoints != null)
                    {
                        for (int i = 0; i < curveSource.controlPoints.Count; ++i)
                            controlPoints.Add(PhysicsGeneratedDtoAdapter.ToVector3(curveSource.controlPoints[i]));
                    }
                    break;

                case SharedGen.OrbitSourceDto orbitSource:
                    orbit.center = PhysicsGeneratedDtoAdapter.ToVector3(orbitSource.fixedCenter);
                    orbit.axis   = PhysicsGeneratedDtoAdapter.ToVector3(orbitSource.customPlaneNormal);
                    orbit.radius = orbitSource.radius;
                    loop         = PhysicsGeneratedDtoAdapter.ToRuntimeOrbitEndMode(orbitSource.endMode) == eOrbitEndMode.Loop;
                    networkPoseComputeDerivedVelocity = orbitSource.computeDerivedVelocity;
                    break;

                case SharedGen.FollowSourceDto followSource:
                    follow.offset = PhysicsGeneratedDtoAdapter.ToVector3(followSource.offset);
                    networkPoseComputeDerivedVelocity = followSource.computeDerivedVelocity;
                    break;

                case SharedGen.NetworkPoseSourceDto networkPoseSource:
                    networkPoseComputeDerivedVelocity = networkPoseSource.computeDerivedVelocity;
                    break;
            }
        }

        private void OnValidate()
        {
            EnsureAssetNameInitialized();
        }
    }
    
} // namespace JamUnity.Authoring.Physics
