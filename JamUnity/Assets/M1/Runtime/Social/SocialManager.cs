using System;
using System.Text;
using JamUnity.Core.Native;

namespace JamUnity.Runtime.Client
{
    public readonly struct SocialMessage
    {
        public readonly ulong MessageId;
        public readonly ulong SenderUserId;
        public readonly CoreNative.eSocialAudience Audience;
        public readonly ulong ScopeId;
        public readonly ushort ContentType;
        public readonly byte[] Payload;

        public SocialMessage(
            ulong messageId,
            ulong senderUserId,
            CoreNative.eSocialAudience audience,
            ulong scopeId,
            ushort contentType,
            byte[] payload)
        {
            MessageId = messageId;
            SenderUserId = senderUserId;
            Audience = audience;
            ScopeId = scopeId;
            ContentType = contentType;
            Payload = payload ?? Array.Empty<byte>();
        }
    }

    public sealed class SocialManager : INativeSocialEventSink
    {
        public const ushort TextChatContentType = 1;
        public const int MaxChatTextBytes = 512;

        private readonly NativeManager nativeManager;

        public event Action<SocialMessage> MessageReceived;

        public SocialManager(NativeManager nativeManager)
        {
            this.nativeManager = nativeManager ?? throw new ArgumentNullException(nameof(nativeManager));
        }

        public CoreNative.eResult SendGlobalText(
            string text,
            out CoreNative.ClientRequestSubmission submission)
        {
            submission = default;
            if (string.IsNullOrWhiteSpace(text))
                return CoreNative.eResult.InvalidArgument;

            byte[] payload = Encoding.UTF8.GetBytes(text.Trim());
            if (payload.Length == 0 || payload.Length > MaxChatTextBytes)
                return CoreNative.eResult.InvalidArgument;

            return nativeManager.RequestSocialCommand(
                CoreNative.eSocialAudience.Global,
                0,
                TextChatContentType,
                payload,
                out submission);
        }

        public void OnSocialMessageReceived(SocialMessage message)
        {
            if (message.ContentType != TextChatContentType)
                return;
            MessageReceived?.Invoke(message);
        }

        public static bool TryDecodeText(SocialMessage message, out string text)
        {
            text = string.Empty;
            if (message.ContentType != TextChatContentType || message.Payload.Length == 0)
                return false;
            text = Encoding.UTF8.GetString(message.Payload);
            return true;
        }
    }
}
