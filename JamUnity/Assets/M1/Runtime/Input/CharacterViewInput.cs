using UnityEngine;

namespace JamUnity.Runtime.Client
{
    internal sealed class CharacterViewInput
    {
        private readonly Transform viewSource;

        private float previousViewYaw;
        private float previousViewPitch;
        private bool hasSample;

        public CharacterViewInput(Transform viewSource)
        {
            this.viewSource = viewSource;
        }

        public bool TrySample(out float viewYaw, out float viewPitch)
        {
            viewYaw = 0.0f;
            viewPitch = 0.0f;
            if (viewSource != null)
            {
                Vector3 euler = viewSource.eulerAngles;
                viewYaw = euler.y * Mathf.Deg2Rad;
                viewPitch = Mathf.DeltaAngle(0.0f, euler.x) * Mathf.Deg2Rad;
            }

            bool changed = !hasSample
                || !Mathf.Approximately(viewYaw, previousViewYaw)
                || !Mathf.Approximately(viewPitch, previousViewPitch);
            previousViewYaw = viewYaw;
            previousViewPitch = viewPitch;
            hasSample = true;
            return changed;
        }

        public void Reset()
        {
            previousViewYaw = 0.0f;
            previousViewPitch = 0.0f;
            hasSample = false;
        }
    }
}
