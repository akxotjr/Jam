using JamUnity.Core.Native;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;

namespace JamUnity.Runtime.Client
{
    internal readonly struct CharacterLocomotionSample
    {
        public readonly CoreNative.eCharacterLocomotionKind Kind;
        public readonly CoreNative.Vec3 Vector;
        public readonly CoreNative.Vec3 RayOrigin;
        public readonly CoreNative.Vec3 RayDirection;
        public readonly float MaxRange;
        public readonly uint TargetActorId;

        private CharacterLocomotionSample(
            CoreNative.eCharacterLocomotionKind kind,
            CoreNative.Vec3 vector = default,
            CoreNative.Vec3 rayOrigin = default,
            CoreNative.Vec3 rayDirection = default,
            float maxRange = 0.0f,
            uint targetActorId = 0)
        {
            Kind = kind;
            Vector = vector;
            RayOrigin = rayOrigin;
            RayDirection = rayDirection;
            MaxRange = maxRange;
            TargetActorId = targetActorId;
        }

        public static CharacterLocomotionSample Stop()
        {
            return new CharacterLocomotionSample(CoreNative.eCharacterLocomotionKind.Stop);
        }

        public static CharacterLocomotionSample Directional(float localX, float localY)
        {
            return new CharacterLocomotionSample(
                CoreNative.eCharacterLocomotionKind.Directional,
                new CoreNative.Vec3(localX, localY, 0.0f));
        }

        public static CharacterLocomotionSample WorldRay(Ray ray, float maxRange)
        {
            return new CharacterLocomotionSample(
                CoreNative.eCharacterLocomotionKind.WorldRay,
                rayOrigin: CoreNative.Vec3.FromUnity(ray.origin),
                rayDirection: CoreNative.Vec3.FromUnity(ray.direction),
                maxRange: maxRange);
        }
    }

    internal interface ICharacterLocomotionInput
    {
        bool TrySample(float deltaTime, out CharacterLocomotionSample sample);
        void Reset();
    }

    internal sealed class DirectionalLocomotionInput : ICharacterLocomotionInput
    {
        private float previousLocalX;
        private float previousLocalY;
        private bool hasSample;

        public bool TrySample(float deltaTime, out CharacterLocomotionSample sample)
        {
            _ = deltaTime;

            float localX = 0.0f;
            float localY = 0.0f;
            if (Keyboard.current != null)
            {
                if (Keyboard.current.aKey.isPressed) localX -= 1.0f;
                if (Keyboard.current.dKey.isPressed) localX += 1.0f;
                if (Keyboard.current.sKey.isPressed) localY -= 1.0f;
                if (Keyboard.current.wKey.isPressed) localY += 1.0f;
            }

            if (hasSample && localX == previousLocalX && localY == previousLocalY)
            {
                sample = default;
                return false;
            }

            previousLocalX = localX;
            previousLocalY = localY;
            hasSample = true;
            sample = localX == 0.0f && localY == 0.0f
                ? CharacterLocomotionSample.Stop()
                : CharacterLocomotionSample.Directional(localX, localY);
            return true;
        }

        public void Reset()
        {
            previousLocalX = 0.0f;
            previousLocalY = 0.0f;
            hasSample = false;
        }
    }

    internal sealed class PointAndClickLocomotionInput : ICharacterLocomotionInput
    {
        private const float DefaultMaxRange = 1000.0f;

        private readonly Camera pointerCamera;
        private readonly float maxRange;

        public PointAndClickLocomotionInput(Camera pointerCamera, float maxRange)
        {
            this.pointerCamera = pointerCamera;
            this.maxRange = maxRange > 0.0f ? maxRange : DefaultMaxRange;
        }

        public bool TrySample(float deltaTime, out CharacterLocomotionSample sample)
        {
            _ = deltaTime;
            sample = default;

            Mouse mouse = Mouse.current;
            if (mouse == null || IsPointerOverUi())
                return false;

            if (mouse.rightButton.wasPressedThisFrame)
            {
                sample = CharacterLocomotionSample.Stop();
                return true;
            }

            if (!mouse.leftButton.wasPressedThisFrame)
                return false;

            Camera camera = pointerCamera != null ? pointerCamera : Camera.main;
            if (camera == null)
                return false;

            Ray ray = camera.ScreenPointToRay(mouse.position.ReadValue());
            sample = CharacterLocomotionSample.WorldRay(ray, maxRange);
            return true;
        }

        public void Reset()
        {
        }

        private static bool IsPointerOverUi()
        {
            return EventSystem.current != null && EventSystem.current.IsPointerOverGameObject();
        }
    }
}
