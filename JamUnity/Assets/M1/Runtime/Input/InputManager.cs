using JamUnity.Core.Native;
using UnityEngine;

namespace JamUnity.Runtime.Client
{
    public sealed class InputManager
    {
        public struct Config
        {
            public Camera PointerCamera;
            public CameraProfile CameraProfile;
            public MovementProfile MovementProfile;
        }

        private ICharacterLocomotionInput directionalLocomotion;
        private ICharacterLocomotionInput pointAndClickLocomotion;
        private ICharacterLocomotionInput activeLocomotion;
        private CharacterActionInput actionInput;
        private CharacterViewInput viewInput;
        private Camera pointerCamera;

        private CameraProfile cameraProfile;
        private MovementProfile movementProfile;
        private CharacterLocomotionSample locomotionSample;
        private bool locomotionDirty;
        private bool inputSuppressed;

        public CameraProfile CameraProfile => cameraProfile;
        public MovementProfile MovementProfile => movementProfile;
        public float OrbitYaw => viewInput != null ? viewInput.ViewYaw : 0.0f;
        public float OrbitPitch => viewInput != null ? viewInput.ViewPitch : 0.0f;

        public void Init(Config config)
        {
            if (!IsValidProfileCombination(config.CameraProfile, config.MovementProfile))
                throw new System.ArgumentException($"Unsupported input profile combination: camera={config.CameraProfile?.name}, movement={config.MovementProfile?.name}");

            pointerCamera = config.PointerCamera;
            directionalLocomotion = new DirectionalLocomotionInput();
            pointAndClickLocomotion = new PointAndClickLocomotionInput(
                pointerCamera,
                config.MovementProfile.PointAndClickMaxRange);
            actionInput = new CharacterActionInput();
            viewInput = new CharacterViewInput(config.CameraProfile);

            cameraProfile = config.CameraProfile;
            movementProfile = config.MovementProfile;
            activeLocomotion = ResolveLocomotion(movementProfile.Kind);
            Reset();
        }

        public bool SetCameraProfile(CameraProfile profile)
        {
            return TrySetProfiles(profile, movementProfile);
        }

        public bool SetMovementProfile(MovementProfile profile)
        {
            return TrySetProfiles(cameraProfile, profile);
        }

        public bool TrySetProfiles(CameraProfile nextCameraProfile, MovementProfile nextMovementProfile)
        {
            if (!IsValidProfileCombination(nextCameraProfile, nextMovementProfile))
                return false;

            if (activeLocomotion != null
                && cameraProfile == nextCameraProfile
                && movementProfile == nextMovementProfile)
                return true;

            activeLocomotion?.Reset();
            cameraProfile = nextCameraProfile;
            movementProfile = nextMovementProfile;
            pointAndClickLocomotion = new PointAndClickLocomotionInput(
                pointerCamera,
                movementProfile.PointAndClickMaxRange);
            activeLocomotion = ResolveLocomotion(nextMovementProfile.Kind);
            viewInput?.SetProfile(nextCameraProfile);
            locomotionSample = CharacterLocomotionSample.Stop();
            locomotionDirty = true;
            return true;
        }

        public static bool IsValidProfileCombination(CameraProfile camera, MovementProfile movement)
        {
            return camera != null
                && movement != null
                && (camera.Kind != CameraProfileKind.ThirdPerson
                    || movement.Kind == MovementProfileKind.Directional);
        }

        public void Reset()
        {
            activeLocomotion?.Reset();
            actionInput?.Reset();
            viewInput?.Reset();
            locomotionSample = CharacterLocomotionSample.Stop();
            locomotionDirty = true;
        }

        public void SetInputSuppressed(bool suppressed)
        {
            if (inputSuppressed == suppressed)
                return;

            inputSuppressed = suppressed;
            if (suppressed)
                viewInput?.ReleaseCursor();
            activeLocomotion?.Reset();
            actionInput?.Reset();
            locomotionSample = CharacterLocomotionSample.Stop();
            locomotionDirty = true;
        }

        public void ReleasePointerLock()
        {
            viewInput?.ReleaseCursor();
        }

        public bool TryBuildIntent(float deltaTime, out CoreNative.CharacterControlIntent intent)
        {
            intent = default;
            if (activeLocomotion == null || actionInput == null || viewInput == null)
                return false;

            if (inputSuppressed)
            {
                if (!locomotionDirty)
                    return false;

                intent = new CoreNative.CharacterControlIntent
                {
                    viewPolicy = CoreNative.eCharacterViewPolicy.FollowMovement,
                    continuousActions = CoreNative.eCharacterActionFlag.None,
                    edgeActions = CoreNative.eCharacterActionFlag.None,
                    locomotion = CoreNative.eCharacterLocomotionKind.Stop,
                };
                locomotionDirty = false;
                return true;
            }

            if (activeLocomotion.TrySample(deltaTime, out CharacterLocomotionSample nextLocomotion))
            {
                locomotionSample = nextLocomotion;
                locomotionDirty = true;
            }

            bool actionChanged = actionInput.TrySample(
                out CoreNative.eCharacterActionFlag continuousActions,
                out CoreNative.eCharacterActionFlag edgeActions);
            float moveReferenceYaw = 0.0f;
            float viewYaw = 0.0f;
            float viewPitch = 0.0f;
            bool viewChanged = false;
            CoreNative.eCharacterViewPolicy viewPolicy = CoreNative.eCharacterViewPolicy.FollowMovement;
            if (cameraProfile.Kind == CameraProfileKind.ThirdPerson)
            {
                viewChanged = viewInput.TrySample(deltaTime, out viewYaw, out viewPitch);
                moveReferenceYaw = viewYaw;
                viewPolicy = CoreNative.eCharacterViewPolicy.Explicit;
            }

            if (!locomotionDirty && !actionChanged && !viewChanged)
                return false;

            intent = new CoreNative.CharacterControlIntent
            {
                moveReferenceYaw = moveReferenceYaw,
                viewYaw = viewYaw,
                viewPitch = viewPitch,
                viewPolicy = viewPolicy,
                continuousActions = continuousActions,
                edgeActions = edgeActions,
                locomotion = locomotionSample.Kind,
                vector = locomotionSample.Vector,
                rayOrigin = locomotionSample.RayOrigin,
                rayDirection = locomotionSample.RayDirection,
                maxRange = locomotionSample.MaxRange,
                targetActorId = locomotionSample.TargetActorId,
            };
            locomotionDirty = false;
            return true;
        }

        private ICharacterLocomotionInput ResolveLocomotion(MovementProfileKind profile)
        {
            return profile switch
            {
                MovementProfileKind.Directional => directionalLocomotion,
                MovementProfileKind.PointAndClick => pointAndClickLocomotion,
                _ => directionalLocomotion,
            };
        }
    }
}
