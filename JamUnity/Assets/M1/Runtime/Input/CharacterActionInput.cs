using JamUnity.Core.Native;
using UnityEngine.InputSystem;

namespace JamUnity.Runtime.Client
{
    internal sealed class CharacterActionInput
    {
        private CoreNative.eCharacterActionFlag previousContinuousActions;
        private bool hasSample;

        public bool TrySample(
            out CoreNative.eCharacterActionFlag continuousActions,
            out CoreNative.eCharacterActionFlag edgeActions)
        {
            continuousActions = CoreNative.eCharacterActionFlag.None;
            edgeActions = CoreNative.eCharacterActionFlag.None;

            if (Keyboard.current != null)
            {
                if (Keyboard.current.spaceKey.wasPressedThisFrame) edgeActions |= CoreNative.eCharacterActionFlag.Jump;
                if (Keyboard.current.qKey.wasPressedThisFrame) edgeActions |= CoreNative.eCharacterActionFlag.Dash;
                if (Keyboard.current.leftShiftKey.isPressed) continuousActions |= CoreNative.eCharacterActionFlag.Sprint;
                if (Keyboard.current.eKey.isPressed) continuousActions |= CoreNative.eCharacterActionFlag.Run;
                if (Keyboard.current.cKey.isPressed) continuousActions |= CoreNative.eCharacterActionFlag.Crouch;
                if (Keyboard.current.zKey.isPressed) continuousActions |= CoreNative.eCharacterActionFlag.Prone;
            }

            bool changed = !hasSample
                || continuousActions != previousContinuousActions
                || edgeActions != CoreNative.eCharacterActionFlag.None;
            previousContinuousActions = continuousActions;
            hasSample = true;
            return changed;
        }

        public void Reset()
        {
            previousContinuousActions = CoreNative.eCharacterActionFlag.None;
            hasSample = false;
        }
    }
}
