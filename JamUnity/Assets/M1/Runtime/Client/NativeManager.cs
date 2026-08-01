using JamUnity.Core.Native;
using System;
using System.Runtime.InteropServices;
using JamUnity.Core.Util;
using UnityEngine;

namespace JamUnity.Runtime.Client
{
    public sealed class NativeManager
    {
        private const int MaxEventsPerFrame = 128;

        private INativeNetworkEventSink networkEventSink;
        private INativeWorldEventSink worldEventSink;
        private INativeActorEventSink actorEventSink;
        private INativeSocialEventSink socialEventSink;
        private bool nativeInitialized;

        public bool IsInitialized => nativeInitialized;
        public event Action<CoreNative.ActorActionRequestCompletedEvent> ActorActionRequestCompleted;

        public void BindNetworkEvent(INativeNetworkEventSink sink) => networkEventSink = sink;
        public void BindWorldEvent(INativeWorldEventSink sink) => worldEventSink = sink;
        public void BindActorEvent(INativeActorEventSink sink) => actorEventSink = sink;
        public void BindSocialEvent(INativeSocialEventSink sink) => socialEventSink = sink;

        public bool Init(ClientRoot.Config config)
        {
            if (nativeInitialized)
            {
                Debug.Log("NativeManager is already initialized.");
                return true;
            }    
            if (!CoreNative.IsAvailable)
            {
                Debug.Log("NativeManager failed to initialize native client: CoreNative is not available.");
                return false;
            }

            CoreNative.ClientConfig nativeConfig = CreateNativeConfig(config);

            if (CoreNative.Initialize(ref nativeConfig) != CoreNative.eResult.Ok)
            {
                Debug.Log("NativeManager failed to initialize native client.");
                return false;
            }
            if (CoreNative.Connect() != CoreNative.eResult.Ok)
            {
                CoreNative.Shutdown();
                Debug.Log("NativeManager failed to connect native client.");
                return false;
            }

            nativeInitialized = true;
            if (config.LogNativeMessages)
                Debug.Log("NativeManager initialized and connect admitted.");
            return true;
        }

        public void Shutdown()
        {
            if (!nativeInitialized)
                return;
            CoreNative.Disconnect();
            CoreNative.Shutdown();
            nativeInitialized = false;
        }

        public CoreNative.eResult SubmitCharacterControl(ref CoreNative.CharacterControlIntent intent)
        {
            if (!nativeInitialized)
                return CoreNative.eResult.NotInitialized;
            intent.structSize = (uint)Marshal.SizeOf<CoreNative.CharacterControlIntent>();
            return CoreNative.SubmitCharacterControl(ref intent);
        }

        public CoreNative.eResult RequestWorldAction(ref CoreNative.WorldActionCommand command, out CoreNative.ClientRequestSubmission submission)
        {
            submission = default;
            if (!nativeInitialized)
                return CoreNative.eResult.NotInitialized;
            command.structSize = (uint)Marshal.SizeOf<CoreNative.WorldActionCommand>();
            return CoreNative.RequestWorldAction(ref command, out submission);
        }

        public CoreNative.eResult RequestActorAction(ref CoreNative.ActorActionCommand command, out CoreNative.ClientRequestSubmission submission)
        {
            submission = default;
            if (!nativeInitialized)
                return CoreNative.eResult.NotInitialized;
            command.structSize = (uint)Marshal.SizeOf<CoreNative.ActorActionCommand>();
            return CoreNative.RequestActorAction(ref command, out submission);
        }

        public CoreNative.eResult RequestSocialCommand(
            CoreNative.eSocialAudience audience,
            ulong scopeId,
            ushort contentType,
            byte[] payload,
            out CoreNative.ClientRequestSubmission submission)
        {
            submission = default;
            if (!nativeInitialized)
                return CoreNative.eResult.NotInitialized;
            if (payload == null)
                return CoreNative.eResult.InvalidArgument;

            GCHandle payloadHandle = default;
            try
            {
                CoreNative.SocialCommand command = new()
                {
                    structSize = (uint)Marshal.SizeOf<CoreNative.SocialCommand>(),
                    destination = new CoreNative.SocialAddress
                    {
                        audience = audience,
                        scopeId = scopeId,
                    },
                    contentType = contentType,
                    payloadSize = (uint)payload.Length,
                };
                if (payload.Length > 0)
                {
                    payloadHandle = GCHandle.Alloc(payload, GCHandleType.Pinned);
                    command.payload = payloadHandle.AddrOfPinnedObject();
                }
                return CoreNative.RequestSocialCommand(ref command, out submission);
            }
            finally
            {
                if (payloadHandle.IsAllocated)
                    payloadHandle.Free();
            }
        }

        public void Process(float deltaTime)
        {
            _ = deltaTime;
            if (!nativeInitialized)
                return;

            CoreNative.ClientPumpOptions options = new()
            {
                structSize = (uint)Marshal.SizeOf<CoreNative.ClientPumpOptions>(),
                maxControlEvents = MaxEventsPerFrame,
            };
            if (CoreNative.Pump(ref options, out _) == CoreNative.eResult.Ok)
                ConsumeNativeEvents();
        }

        public bool TryGetMainWorldRef(out CoreNative.WorldRuntimeRef world)
        {
            world = default;
            return nativeInitialized && CoreNative.GetMainWorldRef(out world) == CoreNative.eResult.Ok;
        }

        public CoreNative.eResult CopyActorFramePair(ulong worldId, CoreNative.ActorState[] previousActors, int previousCapacity, out CoreNative.ActorFrame previousFrame, CoreNative.ActorState[] currentActors, int currentCapacity, out CoreNative.ActorFrame currentFrame, out CoreNative.FrameCopyInfo info)
        {
            previousFrame = default;
            currentFrame = default;
            info = default;
            return !nativeInitialized
                ? CoreNative.eResult.NotInitialized
                : CoreNative.GetActorPresentationFramePair(worldId, previousActors, previousCapacity, out previousFrame, currentActors, currentCapacity, out currentFrame, out info);
        }

        private CoreNative.ClientConfig CreateNativeConfig(ClientRoot.Config config)
        {
            ulong actualAccountId = config.AccountId;
            
#if UNITY_EDITOR
            actualAccountId += (ulong)(MultiPlayModeUtility.GetPlayerIndex() - 1);
#endif
            return new CoreNative.ClientConfig
            {
                structSize = (uint)Marshal.SizeOf<CoreNative.ClientConfig>(),
                abiVersion = CoreNative.AbiVersion,
                serverIp = config.ServerIp,
                tcpPort = config.TcpPort,
                udpPort = config.UdpPort,
                accountId = actualAccountId,
                sharedDataManifestPath = config.SharedDataManifestPath?.Trim() ?? string.Empty,
                headlessMode = config.HeadlessMode ? 1 : 0,
            };
        }

        private void ConsumeNativeEvents()
        {
            for (int processed = 0; processed < MaxEventsPerFrame; ++processed)
            {
                CoreNative.eResult result = CoreNative.PollEvent(out CoreNative.ClientEvent ev);
                if (result == CoreNative.eResult.NoEvent)
                    break;
                if (result != CoreNative.eResult.Ok)
                    return;

                switch (ev.type)
                {
                    case CoreNative.eClientEventType.NetworkStateChanged:
                        networkEventSink?.OnNetworkStateChanged(ev.networkStateChanged);
                        break;
                    case CoreNative.eClientEventType.WorldParticipantChanged:
                        worldEventSink?.OnWorldParticipantChanged(ev.worldParticipantChanged);
                        break;
                    case CoreNative.eClientEventType.ActorLifecycleChanged:
                        actorEventSink?.OnActorLifecycleChanged(ev.actorLifecycleChanged);
                        break;
                    case CoreNative.eClientEventType.WorldRayResolved:
                        worldEventSink?.OnWorldRayResolved(ev.worldRayResolved);
                        break;
                    case CoreNative.eClientEventType.ActorActionRequestCompleted:
                        ActorActionRequestCompleted?.Invoke(ev.actorActionRequestCompleted);
                        break;
                    case CoreNative.eClientEventType.SocialMessageReceived:
                    {
                        CoreNative.SocialMessageReceivedEvent nativeMessage = ev.socialMessageReceived;
                        if (nativeMessage.payloadSize > int.MaxValue)
                            return;
                        int payloadSize = (int)nativeMessage.payloadSize;
                        byte[] payload = new byte[payloadSize];
                        if (payload.Length > 0)
                        {
                            if (nativeMessage.payload == IntPtr.Zero)
                                return;
                            Marshal.Copy(nativeMessage.payload, payload, 0, payload.Length);
                        }
                        socialEventSink?.OnSocialMessageReceived(new SocialMessage(
                            nativeMessage.messageId,
                            nativeMessage.senderUserId,
                            nativeMessage.destination.audience,
                            nativeMessage.destination.scopeId,
                            nativeMessage.contentType,
                            payload));
                        break;
                    }
                }
            }
        }
    }
}
