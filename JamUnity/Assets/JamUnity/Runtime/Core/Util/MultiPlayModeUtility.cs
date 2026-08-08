using System;

namespace JamUnity.Core.Util
{
    public static class MultiPlayModeUtility
    {
        public static int GetPlayerIndex()
        {
#if UNITY_EDITOR
            string[] args = Environment.GetCommandLineArgs();
            
            for (int i = 0; i < args.Length; ++i)
            {
                if (!string.Equals(args[i], "-name", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                string playerName = args[i + 1];
                const string prefix = "Player";

                if (playerName.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) && int.TryParse(playerName.AsSpan(prefix.Length), out int playerIndex))
                {
                    return playerIndex;
                }
            }
#endif
            // treated as Player1 when running the standard Editor or in a standalone build
            return 1;
        }
    }
}