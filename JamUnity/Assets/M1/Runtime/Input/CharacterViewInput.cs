using System.Collections.Generic;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;
using UnityEngine.UI;

namespace JamUnity.Runtime.Client
{
    internal sealed class CharacterViewInput
    {
        private CameraProfile profile;
        private float viewYaw;
        private float viewPitch;
        private float publishElapsed;
        private bool publishPending;

        public float ViewYaw => viewYaw;
        public float ViewPitch => viewPitch;

        public CharacterViewInput(CameraProfile profile)
        {
            SetProfile(profile);
            ReleaseCursor();
        }

        public void SetProfile(CameraProfile nextProfile)
        {
            profile = nextProfile;
            if (profile == null || profile.Kind != CameraProfileKind.ThirdPerson)
                ReleaseCursor();
            Reset();
        }

        public bool TrySample(float deltaTime, out float sampledViewYaw, out float sampledViewPitch)
        {
            publishElapsed += Mathf.Max(deltaTime, 0.0f);

            Mouse mouse = Mouse.current;
            Keyboard keyboard = Keyboard.current;
            if (keyboard != null && keyboard.escapeKey.wasPressedThisFrame)
                ReleaseCursor();

            if (profile != null && mouse != null && Cursor.lockState != CursorLockMode.Locked
                && mouse.leftButton.wasPressedThisFrame
                && !IsPointerOverUi(mouse.position.ReadValue()))
            {
                Cursor.lockState = CursorLockMode.Locked;
                Cursor.visible = false;
            }

            if (profile != null && mouse != null && Cursor.lockState == CursorLockMode.Locked)
            {
                Vector2 delta = mouse.delta.ReadValue();
                if (delta.sqrMagnitude > 0.0f)
                {
                    viewYaw = Mathf.Repeat(
                        viewYaw + delta.x * profile.MouseOrbitSensitivity,
                        360.0f);
                    viewPitch = Mathf.Clamp(
                        viewPitch - delta.y * profile.MouseOrbitSensitivity,
                        profile.OrbitPitchLimits.x,
                        profile.OrbitPitchLimits.y);
                    publishPending = true;
                }
            }

            ResolveViewAngles(out sampledViewYaw, out sampledViewPitch);

            float publishInterval = profile != null
                ? 1.0f / Mathf.Max(profile.OrbitInputRate, 1.0f)
                : 0.0f;
            if (!publishPending || publishElapsed < publishInterval)
                return false;

            publishElapsed = 0.0f;
            publishPending = false;
            return true;
        }

        private static bool IsPointerOverUi(Vector2 position)
        {
            EventSystem eventSystem = EventSystem.current;
            if (eventSystem == null)
                return false;

            // Check the current position even if the UI input module has not updated yet.
            var pointer = new PointerEventData(eventSystem) { position = position };
            var hits = new List<RaycastResult>();
            eventSystem.RaycastAll(pointer, hits);
            foreach (RaycastResult hit in hits)
            {
                if (hit.module is GraphicRaycaster)
                    return true;
            }

            return false;
        }

        private void ResolveViewAngles(out float yaw, out float pitch)
        {
            if (profile == null)
            {
                yaw = 0.0f;
                pitch = 0.0f;
                return;
            }

            Vector3 orbitOffset = Quaternion.Euler(viewPitch, viewYaw, 0.0f) * profile.FollowOffset;
            Vector3 viewDirection = profile.LookAtOffset - orbitOffset;
            float horizontal = new Vector2(viewDirection.x, viewDirection.z).magnitude;
            yaw = Mathf.Atan2(viewDirection.x, viewDirection.z);
            pitch = Mathf.Atan2(-viewDirection.y, horizontal);
        }

        public void Reset()
        {
            viewYaw = profile != null ? profile.InitialOrbitYaw : 0.0f;
            viewPitch = profile != null
                ? Mathf.Clamp(profile.InitialOrbitPitch, profile.OrbitPitchLimits.x, profile.OrbitPitchLimits.y)
                : 0.0f;
            publishElapsed = 0.0f;
            publishPending = true;
        }

        public void ReleaseCursor()
        {
            if (Cursor.lockState != CursorLockMode.None)
                Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;
        }
    }
}
