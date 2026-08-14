using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace JamUnity.Core.Native
{
    public static class CoreNative
    {
        public const uint AbiVersion = 15;

        private const string DllName =
#if JAM_NATIVE_DEBUG
            "JamUnityBridge_d";
#else
            "JamUnityBridge";
#endif

        public enum eResult : int
        {
            Ok = 0,
            NoEvent = 1,
            NotInitialized = -1,
            InvalidArgument = -2,
            NotConnected = -3,
            BufferTooSmall = -4,
            VersionMismatch = -5,
            InternalError = -6,
        }

		public enum eNetworkPhase : byte
        {
            Disconnected,
            Connecting,
            Ready,
			Degraded
		}

		public enum eBootstrapKind : byte
		{
			Pending,
			Fresh,
			Resync
		}

        public enum eClientRequestKind : byte
        {
            None,
            WorldAction,
            ActorAction,
            SocialCommand,
            ContentRequest
        }

        public enum eClientRequestAdmission : byte
        {
            Accepted,
            NotInitialized,
            NotConnected,
            InvalidArgument,
            QueueFull
        }

        public enum eWorldDestinationSelector : byte
        {
            DefaultForArchetype,
            ExplicitInstance,
            AuthoredDestination
        }

        public enum eWorldActionKind : byte
        {
            Enter,
            Leave
        }

        public enum eActorAction : byte
        {
            Spawn,
            Despawn
        }

        public enum eActorActionStatus : byte
        {
            Succeeded,
            Failed
        }

        public enum eActorActionReason : byte
        {
            None,
            InvalidArgument,
            WorldUnavailable,
            ActorNotFound,
            TransportUnavailable,
            Rejected,
            Shutdown
        }

        public enum eCharacterLocomotionKind : byte
        {
            Stop,
            Directional,
            WorldRay,
            Position,
            FollowActor
        }

        public enum eCharacterViewPolicy : byte
        {
            FollowMovement,
            Explicit,
        }

        public enum eWorldParticipantChange : byte
        {
            Joined,
            Left
        }

        public enum eActorLifecycleReason : byte
        {
            Spawned,
            Despawned,
            AoiEntered,
            AoiLeft,
            LocallyHidden
        }

        public enum eClientEventType : byte
        {
            None,
            NetworkStateChanged,
            WorldParticipantChanged,
            ActorLifecycleChanged,
            WorldRayResolved,
            ActorActionRequestCompleted,
            SocialMessageReceived,
            ContentRequestCompleted
        }

        public enum eContentResponseStatus : byte
        {
            None,
            Succeeded,
            Rejected,
            InvalidRequest,
            Unavailable,
            InternalError
        }

        public enum eSocialAudience : byte
        {
            Direct,
            Group,
            Global
        }

        public enum eSocialRecipientKind : byte
        {
            None,
            AccountId,
            CharacterId,
            CharacterName
        }

        [Flags]
        public enum eCharacterActionFlag : uint
        {
            None = 0,
            Crouch = 1u << 0,
            Prone = 1u << 1,
            Jump = 1u << 2,
            Dash = 1u << 3,
            Run = 1u << 4,
            Sprint = 1u << 5,
        }

        [Flags]
        public enum eActorSpawnOverride : uint
        {
            None = 0,
            LinearVelocity = 1u << 0,
            AngularVelocity = 1u << 1,
            LinearDamping = 1u << 2,
            AngularDamping = 1u << 3,
            ViewYaw = 1u << 4,
            ViewPitch = 1u << 5,
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct Vec3
        {
            public float x;
            public float y;
            public float z;

            public Vec3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
            public Vector3 ToUnity() => new(x, y, z);
            public static Vec3 FromUnity(Vector3 value) => new(value.x, value.y, value.z);
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct Quat
        {
            public float x;
            public float y;
            public float z;
            public float w;

            public Quaternion ToUnity() => new(x, y, z, w);
            public static Quat FromUnity(Quaternion value) => new() { x = value.x, y = value.y, z = value.z, w = value.w };
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ClientConfig
        {
            public uint structSize;
            public uint abiVersion;
            [MarshalAs(UnmanagedType.LPUTF8Str)] public string serverIp;
            public ushort tcpPort;
            public ushort udpPort;
            public ulong accountId;
            [MarshalAs(UnmanagedType.LPUTF8Str)] public string loginId;
            [MarshalAs(UnmanagedType.LPUTF8Str)] public string password;
            public IntPtr ticket;
            public uint ticketSize;
            [MarshalAs(UnmanagedType.LPUTF8Str)] public string sharedDataManifestPath;
            public int headlessMode;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ClientPumpOptions
        {
            public uint structSize;
            public ulong maxControlEvents;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ClientPumpResult
        {
            public uint structSize;
            public ulong appliedControlEvents; 
            public ulong pendingControlEvents; 
            public int presentationUpdated;
        }

        [StructLayout(LayoutKind.Sequential)]
		public struct NetworkState
		{
			public eNetworkPhase phase;
			public eBootstrapKind bootstrapKind;
		}

        [StructLayout(LayoutKind.Sequential)]
        public struct WorldRuntimeRef
        {
            public ulong worldId;
            public ulong worldInstanceId; 
            public ulong worldArchetypeKey;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ClientRequestReceipt
        {
            public ulong requestId;
            public eClientRequestKind kind;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ClientRequestSubmission
        {
            public eClientRequestAdmission admission;
            public ClientRequestReceipt receipt;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct EnterWorldRequest
        {
            public ulong worldArchetypeKey;
            public eWorldDestinationSelector selector;
            public ulong explicitWorldInstanceId;
            [MarshalAs(UnmanagedType.LPUTF8Str)] public string destinationName;
            public ulong expectedMainRevision;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct LeaveWorldRequest
        {
            public ulong expectedMainRevision;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct WorldActionCommand
        {
            public uint structSize;
            public eWorldActionKind kind;
            public EnterWorldRequest enter;
            public LeaveWorldRequest leave;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ActorSpawnSpec
        {
            public ulong actorArchetypeKey;
            public Vec3 position;
            public Quat rotation;
            public int requestOwnership;
            public int requestControl;
            public uint targetActorId;
            public eActorSpawnOverride overrideMask;
            public Vec3 linearVelocity;
            public Vec3 angularVelocity;
            public float linearDamping;
            public float angularDamping;
            public float viewYaw;
            public float viewPitch;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ActorActionCommand
        {
            public uint structSize;
            public eActorAction action;
            public ulong worldId;
            public ActorSpawnSpec spawn;
            public uint targetActorId;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct SocialAddress
        {
            public eSocialAudience audience;
            public ulong scopeId;
            public eSocialRecipientKind recipientKind;
            public ulong recipientId;
            public IntPtr recipientName;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct SocialCommand
        {
            public uint structSize;
            public SocialAddress destination;
            public ushort contentType;
            public IntPtr payload;
            public uint payloadSize;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct GenericContentRequest
        {
            public uint structSize;
            public ulong operationKey;
            public IntPtr payload;
            public uint payloadSize;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct CharacterControlIntent
        {
            public uint structSize;
            public float moveReferenceYaw;
            public float viewYaw;
            public float viewPitch;
            public eCharacterViewPolicy viewPolicy;
            public eCharacterActionFlag continuousActions;
            public eCharacterActionFlag edgeActions;
            public eCharacterLocomotionKind locomotion;
            public Vec3 vector;
            public Vec3 rayOrigin;
            public Vec3 rayDirection;
            public float maxRange;
            public uint targetActorId;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ActorState
        {
            public uint actorId;
            public int isLocal;
            public int hasTransform;
            public Vec3 position;
            public Quat rotation;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ActorFrame
        {
            public ulong sequence;
            public uint tick;
            public float timestamp;
            public int actorCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct FrameCopyInfo
        {
            public uint structSize;
            public int previousRequiredCount;
            public int previousCopiedCount;
            public int currentRequiredCount;
            public int currentCopiedCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct NetworkStateChangedEvent
        {
            public ulong accountId;
            public ulong userId;
            public NetworkState state;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct WorldParticipantChangedEvent
        {
            public ulong accountId;
            public ulong userId;
            public eWorldParticipantChange change;
            public WorldRuntimeRef world;
            public ulong participantUserId;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ActorLifecycleChangedEvent
        {
            public ulong accountId;
            public ulong userId;
            public ulong worldId;
            public ulong clientRequestId;
            public uint actorId;
            public int isLocal;
            public eActorLifecycleReason reason;
            public ulong actorArchetypeKey;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct WorldRayResolvedEvent
        {
            public ulong accountId;
            public ulong userId;
            public ulong worldId;
            public int hit;
            public Vec3 position;
            public Vec3 normal;
            public uint hitActorId;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ActorActionRequestCompletedEvent
        {
            public ClientRequestReceipt receipt;
            public eActorAction action;
            public eActorActionStatus status;
            public eActorActionReason reason;
            public uint actorId;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct SocialMessageReceivedEvent
        {
            public ulong accountId;
            public ulong userId;
            public ulong messageId;
            public ulong senderUserId;
            public SocialAddress destination;
            public ushort contentType;
            public IntPtr payload;
            public uint payloadSize;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct GenericContentRequestCompletedEvent
        {
            public ulong accountId;
            public ulong userId;
            public ulong requestId;
            public ulong operationKey;
            public eContentResponseStatus status;
            public uint resultCode;
            public IntPtr payload;
            public uint payloadSize;
        }

        [StructLayout(LayoutKind.Explicit, Size = 104)]
        public struct ClientEvent
        {
            [FieldOffset(0)] public uint structSize;
            [FieldOffset(4)] public eClientEventType type;
            [FieldOffset(8)] public NetworkStateChangedEvent networkStateChanged;
            [FieldOffset(8)] public WorldParticipantChangedEvent worldParticipantChanged;
            [FieldOffset(8)] public ActorLifecycleChangedEvent actorLifecycleChanged;
            [FieldOffset(8)] public WorldRayResolvedEvent worldRayResolved;
            [FieldOffset(8)] public ActorActionRequestCompletedEvent actorActionRequestCompleted;
            [FieldOffset(8)] public SocialMessageReceivedEvent socialMessageReceived;
            [FieldOffset(8)] public GenericContentRequestCompletedEvent contentRequestCompleted;
        }

        public static bool IsAvailable
        {
            get
            {
                try { return JU_GetAbiVersion() == AbiVersion; }
                catch (Exception e) when (e is DllNotFoundException || e is EntryPointNotFoundException)
                {
                    Debug.LogError($"JamUnityBridge unavailable: {e.Message}");
                    return false;
                }
            }
        }

        public static eResult Initialize(ref ClientConfig config) => JU_Initialize(ref config);
        public static void Shutdown() => JU_Shutdown();
        public static eResult Connect() => JU_Connect();
        public static void Disconnect() => JU_Disconnect();
        public static eResult Pump(ref ClientPumpOptions options, out ClientPumpResult result) => JU_Pump(ref options, out result);
        public static eResult GetNetworkState(out NetworkState state) => JU_GetNetworkState(out state);
        public static eResult GetAccountId(out ulong accountId) => JU_GetAccountId(out accountId);
        public static eResult GetUserId(out ulong userId) => JU_GetUserId(out userId);
        public static eResult GetMainWorldRef(out WorldRuntimeRef world) => JU_GetMainWorldRef(out world);
        public static eResult GetActorPresentationFramePair(ulong worldId, ActorState[] previousActors, int previousCapacity, out ActorFrame previousFrame, ActorState[] currentActors, int currentCapacity, out ActorFrame currentFrame, out FrameCopyInfo info) =>
            JU_GetActorPresentationFramePair(worldId, previousActors, previousCapacity, out previousFrame, currentActors, currentCapacity, out currentFrame, out info);
        public static eResult RequestWorldAction(ref WorldActionCommand command, out ClientRequestSubmission submission) => JU_RequestWorldAction(ref command, out submission);
        public static eResult RequestActorAction(ref ActorActionCommand command, out ClientRequestSubmission submission) => JU_RequestActorAction(ref command, out submission);
        public static eResult RequestSocialCommand(ref SocialCommand command, out ClientRequestSubmission submission) => JU_RequestSocialCommand(ref command, out submission);
        public static eResult RequestGenericContent(ref GenericContentRequest request, out ClientRequestSubmission submission) => JU_RequestGenericContent(ref request, out submission);
        public static eResult SubmitCharacterControl(ref CharacterControlIntent intent) => JU_SubmitCharacterControl(ref intent);
        public static eResult PollEvent(out ClientEvent ev) => JU_PollEvent(out ev);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern uint JU_GetAbiVersion();
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern eResult JU_Initialize(ref ClientConfig config);
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern void JU_Shutdown();
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern eResult JU_Connect();
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern void JU_Disconnect();
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern eResult JU_Pump(ref ClientPumpOptions options, out ClientPumpResult result);
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern eResult JU_GetNetworkState(out NetworkState state);
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern eResult JU_GetAccountId(out ulong accountId);
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern eResult JU_GetUserId(out ulong userId);
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern eResult JU_GetMainWorldRef(out WorldRuntimeRef world);
    
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern eResult JU_GetActorPresentationFramePair(ulong worldId, [Out] ActorState[] previousActors, int previousCapacity, out ActorFrame previousFrame, [Out] ActorState[] currentActors, int currentCapacity, out ActorFrame currentFrame, out FrameCopyInfo info);
      
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern eResult JU_RequestWorldAction(ref WorldActionCommand command, out ClientRequestSubmission submission);
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern eResult JU_RequestActorAction(ref ActorActionCommand command, out ClientRequestSubmission submission);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern eResult JU_RequestSocialCommand(ref SocialCommand command, out ClientRequestSubmission submission);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern eResult JU_RequestGenericContent(ref GenericContentRequest request, out ClientRequestSubmission submission);
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern eResult JU_SubmitCharacterControl(ref CharacterControlIntent intent);
        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        private static extern eResult JU_PollEvent(out ClientEvent ev);
    }
}
