using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Toolbars;
using UnityEngine;

#if UNITY_EDITOR

namespace JamUnity.Editor
{
    public static class NativeBuildMode
    {
        private const string DebugDefine = "JAM_NATIVE_DEBUG";

        private static readonly NamedBuildTarget BuildTarget = NamedBuildTarget.Standalone;

        public static bool IsDebug
        {
            get
            {
                string defines = PlayerSettings.GetScriptingDefineSymbols(BuildTarget);

                foreach (string define in defines.Split(';'))
                {
                    if (define == DebugDefine)
                        return true;
                }
                return false;
            }
        }

        public static void SetDebug(bool enabled)
        {
            string defines = PlayerSettings.GetScriptingDefineSymbols(BuildTarget);

            var defineList = new List<string>(defines.Split(';', StringSplitOptions.RemoveEmptyEntries));
            defineList.RemoveAll(x => x == DebugDefine);

            if (enabled)
                defineList.Add(DebugDefine);

            PlayerSettings.SetScriptingDefineSymbols(BuildTarget, string.Join(";", defineList));
        }
    }


    public static class NativeBuildModeToolbar
    {
        [MainToolbarElement("Jam/NativeBuildMode")]
        public static MainToolbarElement Create()
        {
            var dropdown = new MainToolbarDropdown(new MainToolbarContent(GetLabel(), null, GetTooltip()), ShowMenu);
            return dropdown;
        }

        private static string GetLabel()
        {
            return NativeBuildMode.IsDebug ? "Native: Debug" : "Native: Release";
        }

        private static string GetTooltip()
        {
            return "Select JamUnityBridge native build configuration.";
        }

        private static void ShowMenu(Rect rect)
        {
            var menu = new GenericMenu();

            menu.AddItem(new GUIContent("Debug"), NativeBuildMode.IsDebug, () => SetMode(true));
            menu.AddItem(new GUIContent("Release"), !NativeBuildMode.IsDebug, () => SetMode(false));

            menu.DropDown(rect);
        }

        private static void SetMode(bool debug)
        {
            if (NativeBuildMode.IsDebug == debug)
                return;

            NativeBuildMode.SetDebug(debug);
            Debug.Log(debug ? "[Jam] Native mode: Debug" : "[Jam] Native mode: Release");
        }
    }

}

#endif