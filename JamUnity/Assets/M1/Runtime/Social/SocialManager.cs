using System;
using System.Text;
using Google.FlatBuffers;
using JamUnity.Core.Native;
using JamUnity.World.Presentation;


namespace JamUnity.Runtime.Client
{
    public readonly struct SocialMessage
    {
        public readonly ulong MessageId;
        public readonly ulong SenderUserId;
        public readonly CoreNative.eSocialAudience Audience;
        public readonly ulong ScopeId;
        public readonly CoreNative.eSocialRecipientKind RecipientKind;
        public readonly ulong RecipientId;
        public readonly string RecipientName;
        public readonly ushort ContentType;
        public readonly byte[] Payload;

        public SocialMessage(ulong messageId, ulong senderUserId, CoreNative.eSocialAudience audience, ulong scopeId,
            CoreNative.eSocialRecipientKind recipientKind, ulong recipientId, string recipientName,
            ushort contentType, byte[] payload)
        {
            MessageId    = messageId;
            SenderUserId = senderUserId;
            Audience     = audience;
            ScopeId      = scopeId;
            RecipientKind = recipientKind;
            RecipientId = recipientId;
            RecipientName = recipientName ?? string.Empty;
            ContentType  = contentType;
            Payload      = payload ?? Array.Empty<byte>();
        }
    }

    public sealed class SocialManager : INativeSocialEventSink
    {
        public const ushort TextChatContentType = 1;
        public const int MaxChatTextBytes = 512;

        private readonly NativeManager nativeManager;
		private readonly WorldPresentationDatabase worldPresentationDatabase;

        public event Action<SocialMessage> MessageReceived;

        public SocialManager(NativeManager nativeManager, WorldPresentationDatabase worldPresentationDatabase)
        {
            this.nativeManager = nativeManager ?? throw new ArgumentNullException(nameof(nativeManager));
			this.worldPresentationDatabase = worldPresentationDatabase;
        }

        public CoreNative.eResult SendGlobalText(string text, out CoreNative.ClientRequestSubmission submission)
        {
            return SendText(CoreNative.eSocialAudience.Global, 0, text, out submission);
        }

        public CoreNative.eResult SendWorldText(string text, out CoreNative.ClientRequestSubmission submission)
        {
            submission = default;
            if (!nativeManager.TryGetMainWorldRef(out CoreNative.WorldRuntimeRef world) || world.worldId == 0)
                return CoreNative.eResult.InvalidArgument;

            return SendText(CoreNative.eSocialAudience.Group, world.worldId, text, out submission);
        }

        public bool TryGetMainWorldId(out ulong worldId)
        {
            worldId = 0;
            if (!nativeManager.TryGetMainWorldRef(out CoreNative.WorldRuntimeRef world) || world.worldId == 0)
                return false;
            worldId = world.worldId;
            return true;
        }

		public bool TryGetMainWorldName(out string worldName)
		{
			worldName = string.Empty;
			if (!nativeManager.TryGetMainWorldRef(out CoreNative.WorldRuntimeRef world)
				|| world.worldArchetypeKey == 0 || worldPresentationDatabase == null
				|| !worldPresentationDatabase.TryGetEntry(world.worldArchetypeKey, out var entry))
				return false;
			worldName = entry.WorldArchetypeName;
			return !string.IsNullOrEmpty(worldName);
		}

        public CoreNative.eResult SendDirectText(string characterName, string text, out CoreNative.ClientRequestSubmission submission)
        {
			characterName = characterName?.Trim() ?? string.Empty;
            if (characterName.Length == 0)
            {
                submission = default;
                return CoreNative.eResult.InvalidArgument;
            }

            return SendText(CoreNative.eSocialAudience.Direct, 0,
				CoreNative.eSocialRecipientKind.CharacterName, 0, characterName, text, out submission);
        }

        private CoreNative.eResult SendText(CoreNative.eSocialAudience audience, ulong scopeId, string text, out CoreNative.ClientRequestSubmission submission)
        {
            return SendText(audience, scopeId, CoreNative.eSocialRecipientKind.None, 0, string.Empty, text, out submission);
        }

        private CoreNative.eResult SendText(CoreNative.eSocialAudience audience, ulong scopeId,
            CoreNative.eSocialRecipientKind recipientKind, ulong recipientId, string recipientName,
            string text, out CoreNative.ClientRequestSubmission submission)
        {
            submission = default;
            if (string.IsNullOrWhiteSpace(text))
                return CoreNative.eResult.InvalidArgument;

			string normalizedText = text.Trim();
			if (Encoding.UTF8.GetByteCount(normalizedText) > MaxChatTextBytes)
                return CoreNative.eResult.InvalidArgument;

			FlatBufferBuilder builder = new(1024);
			StringOffset textOffset = builder.CreateString(normalizedText);
			StringOffset senderOffset = builder.CreateString(string.Empty);
			Offset<m1.fb.fbChatText> root = m1.fb.fbChatText.CreatefbChatText(
				builder, textOffset, senderOffset);
			builder.Finish(root.Value);
			byte[] payload = builder.SizedByteArray();

            return nativeManager.RequestSocialCommand(audience, scopeId, recipientKind, recipientId,
                recipientName, TextChatContentType, payload, out submission);
        }

        public void OnSocialMessageReceived(SocialMessage message)
        {
            if (message.ContentType != TextChatContentType)
                return;
            MessageReceived?.Invoke(message);
        }

		public static bool TryDecodeText(SocialMessage message, out string text, out string senderCharacterName)
        {
            text = string.Empty;
			senderCharacterName = string.Empty;
            if (message.ContentType != TextChatContentType || message.Payload.Length == 0)
                return false;
			try
			{
				m1.fb.fbChatText wire = m1.fb.fbChatText.GetRootAsfbChatText(new ByteBuffer(message.Payload));
				text = wire.Text ?? string.Empty;
				senderCharacterName = wire.SenderCharacterName ?? string.Empty;
				return text.Length > 0 && senderCharacterName.Length > 0;
			}
			catch (Exception)
			{
				text = string.Empty;
				senderCharacterName = string.Empty;
				return false;
			}
        }
    }
}
