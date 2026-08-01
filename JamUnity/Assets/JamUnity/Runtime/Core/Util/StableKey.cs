using System;
using System.Globalization;
using System.Text;

namespace JamUnity.Core.Util
{
    public static class StableKey
    {
        public static ulong MakeStableKey(string name)
        {
            if (string.IsNullOrEmpty(name))
                return 0;

            const ulong offsetBasis = 14695981039346656037UL;
            const ulong prime = 1099511628211UL;

            ulong hash = offsetBasis;
            byte[] bytes = Encoding.UTF8.GetBytes(name);
            for (int i = 0; i < bytes.Length; ++i)
            {
                hash ^= bytes[i];
                hash *= prime;
            }

            return hash;
        }

        public static bool TryParseUlongKey(string raw, out ulong key)
        {
            key = 0;
            if (string.IsNullOrWhiteSpace(raw))
                return false;

            string text = raw.Trim();
            if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                return ulong.TryParse(text.Substring(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out key);

            return ulong.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out key);
        }
    }
}
