using System;
using System.Collections.Generic;
using Google.FlatBuffers;
using JamUnity.Core.Native;

namespace JamUnity.Runtime.Client
{
    public sealed class CharacterContent : IDisposable
    {
        public const ulong CharacterListOperation = 1;
        public const ulong CharacterSelectOperation = 2;

        private readonly NativeManager nativeManager;
        private readonly HashSet<ulong> pendingListRequests = new();
        private readonly HashSet<ulong> pendingSelectRequests = new();
        private bool disposed;

        public event Action<CoreNative.eContentResponseStatus, IReadOnlyList<CharacterSummary>> CharacterListCompleted;
        public event Action<CoreNative.eContentResponseStatus, CharacterSummary> CharacterSelectCompleted;

        public CharacterContent(NativeManager nativeManager)
        {
            this.nativeManager = nativeManager ?? throw new ArgumentNullException(nameof(nativeManager));
            nativeManager.ContentRequestCompleted += OnContentRequestCompleted;
        }

        public CoreNative.eResult RequestCharacterList(out CoreNative.ClientRequestSubmission submission)
        {
            CoreNative.eResult result = nativeManager.RequestContent(
                CharacterListOperation, Array.Empty<byte>(), out submission);
            if (result == CoreNative.eResult.Ok
                && submission.admission == CoreNative.eClientRequestAdmission.Accepted)
                pendingListRequests.Add(submission.receipt.requestId);
            return result;
        }

        public CoreNative.eResult SelectCharacter(
            ulong characterId,
            out CoreNative.ClientRequestSubmission submission)
        {
            submission = default;
            if (characterId == 0)
                return CoreNative.eResult.InvalidArgument;

            FlatBufferBuilder builder = new(64);
            Offset<m1.fb.fbCharacterSelectRequest> root =
                m1.fb.fbCharacterSelectRequest.CreatefbCharacterSelectRequest(builder, characterId);
            builder.Finish(root.Value);

            CoreNative.eResult result = nativeManager.RequestContent(
                CharacterSelectOperation, builder.SizedByteArray(), out submission);
            if (result == CoreNative.eResult.Ok
                && submission.admission == CoreNative.eClientRequestAdmission.Accepted)
                pendingSelectRequests.Add(submission.receipt.requestId);
            return result;
        }

        public void Dispose()
        {
            if (disposed)
                return;
            disposed = true;
            nativeManager.ContentRequestCompleted -= OnContentRequestCompleted;
            pendingListRequests.Clear();
            pendingSelectRequests.Clear();
        }

        private void OnContentRequestCompleted(ContentResponse response)
        {
            if (response.OperationKey == CharacterListOperation
                && pendingListRequests.Remove(response.RequestId))
            {
                HandleCharacterList(response);
                return;
            }

            if (response.OperationKey == CharacterSelectOperation
                && pendingSelectRequests.Remove(response.RequestId))
                HandleCharacterSelect(response);
        }

        private void HandleCharacterList(ContentResponse response)
        {
            if (response.Status != CoreNative.eContentResponseStatus.Succeeded)
            {
                CharacterListCompleted?.Invoke(response.Status, Array.Empty<CharacterSummary>());
                return;
            }

            try
            {
                m1.fb.fbCharacterListResponse wire = m1.fb.fbCharacterListResponse
                    .GetRootAsfbCharacterListResponse(new ByteBuffer(response.Payload));
                var characters = new List<CharacterSummary>(wire.CharactersLength);
                for (int i = 0; i < wire.CharactersLength; ++i)
                {
                    m1.fb.fbCharacterSummary? wireCharacter = wire.Characters(i);
                    if (!wireCharacter.HasValue)
                        throw new InvalidOperationException("Character list contains an empty entry.");
                    CharacterSummary character = CharacterSummary.FromFlatBuffer(wireCharacter.Value);
                    if (!character.IsValid)
                        throw new InvalidOperationException("Character list contains an invalid entry.");
                    characters.Add(character);
                }
                CharacterListCompleted?.Invoke(response.Status, characters);
            }
            catch (Exception)
            {
                CharacterListCompleted?.Invoke(
                    CoreNative.eContentResponseStatus.InternalError,
                    Array.Empty<CharacterSummary>());
            }
        }

        private void HandleCharacterSelect(ContentResponse response)
        {
            if (response.Status != CoreNative.eContentResponseStatus.Succeeded)
            {
                CharacterSelectCompleted?.Invoke(response.Status, default);
                return;
            }

            try
            {
                m1.fb.fbCharacterSelectResponse wire = m1.fb.fbCharacterSelectResponse
                    .GetRootAsfbCharacterSelectResponse(new ByteBuffer(response.Payload));
                m1.fb.fbCharacterSummary? wireCharacter = wire.Character;
                if (!wireCharacter.HasValue)
                    throw new InvalidOperationException("Character select response has no character.");
                CharacterSummary character = CharacterSummary.FromFlatBuffer(wireCharacter.Value);
                if (!character.IsValid)
                    throw new InvalidOperationException("Character select response contains an invalid character.");
                CharacterSelectCompleted?.Invoke(response.Status, character);
            }
            catch (Exception)
            {
                CharacterSelectCompleted?.Invoke(CoreNative.eContentResponseStatus.InternalError, default);
            }
        }
    }
}
