#pragma once

#include <cstdint>

#ifdef _WIN32
#define JAM_API extern "C" __declspec(dllexport)
#else
#define JAM_API extern "C"
#endif

inline constexpr uint32_t JAM_ABI_VERSION = 12;

enum class JAM_eResult : int32_t
{
	Ok					= 0,
	NoEvent				= 1,
	NotInitialized		= -1,
	InvalidArgument		= -2,
	NotConnected		= -3,
	BufferTooSmall		= -4,
	VersionMismatch		= -5,
	InternalError		= -6,
};

enum class JAM_eNetworkPhase : uint8_t
{
	Disconnected,
	Connecting,
	Ready,
	Degraded,
};

enum class JAM_eClientRequestKind : uint8_t
{
	None,
	WorldAction,
	ActorAction,
	SocialCommand,
};

enum class JAM_eClientRequestAdmission : uint8_t
{
	Accepted,
	NotInitialized,
	NotConnected,
	InvalidArgument,
	QueueFull,
};

enum class JAM_eWorldDestinationSelector : uint8_t
{
	DefaultForArchetype,
	ExplicitInstance,
	AuthoredDestination,
};

enum class JAM_eWorldActionKind : uint8_t
{
	Enter,
	Leave,
};

enum class JAM_eActorAction : uint8_t
{
	Spawn,
	Despawn,
};

enum class JAM_eActorActionStatus : uint8_t
{
	Succeeded,
	Failed,
};

enum class JAM_eActorActionReason : uint8_t
{
	None,
	InvalidArgument,
	WorldUnavailable,
	ActorNotFound,
	TransportUnavailable,
	Rejected,
	Shutdown,
};

enum class JAM_eCharacterLocomotionKind : uint8_t
{
	Stop,
	Directional,
	WorldRay,
	Position,
	FollowActor,
};

enum class JAM_eCharacterViewPolicy : uint8_t
{
	FollowMovement,
	Explicit,
};

enum class JAM_eWorldParticipantChange : uint8_t
{
	Joined,
	Left,
};

enum class JAM_eActorLifecycleReason : uint8_t
{
	Spawned,
	Despawned,
	AoiEntered,
	AoiLeft,
	LocallyHidden,
};

enum class JAM_eClientEventType : uint8_t
{
	None,
	NetworkStateChanged,
	WorldParticipantChanged,
	ActorLifecycleChanged,
	WorldRayResolved,
	ActorActionRequestCompleted,
	SocialMessageReceived,
};

enum class JAM_eSocialAudience : uint8_t
{
	Direct,
	Group,
	Global,
};

enum JAM_CharacterActionFlag : uint32_t
{
	CHARACTER_ACTION_NONE	= 0,
	CHARACTER_ACTION_CROUCH	= 1u << 0,
	CHARACTER_ACTION_PRONE	= 1u << 1,
	CHARACTER_ACTION_JUMP	= 1u << 2,
	CHARACTER_ACTION_DASH	= 1u << 3,
	CHARACTER_ACTION_RUN	= 1u << 4,
	CHARACTER_ACTION_SPRINT	= 1u << 5,
};

enum JAM_ActorSpawnOverride : uint32_t
{
	ACTOR_SPAWN_OVERRIDE_NONE				= 0,
	ACTOR_SPAWN_OVERRIDE_LINEAR_VELOCITY	= 1u << 0,
	ACTOR_SPAWN_OVERRIDE_ANGULAR_VELOCITY	= 1u << 1,
	ACTOR_SPAWN_OVERRIDE_LINEAR_DAMPING		= 1u << 2,
	ACTOR_SPAWN_OVERRIDE_ANGULAR_DAMPING	= 1u << 3,
	ACTOR_SPAWN_OVERRIDE_VIEW_YAW			= 1u << 4,
	ACTOR_SPAWN_OVERRIDE_VIEW_PITCH			= 1u << 5,
};

struct JAM_Vec3
{
	float	x; 
	float	y; 
	float	z;
};

struct JAM_Quat
{
	float	x; 
	float	y; 
	float	z; 
	float	w;
};

struct JAM_ClientConfig
{
	uint32_t	structSize;
	uint32_t	abiVersion;
	const char*	serverIp;
	uint16_t	tcpPort;
	uint16_t	udpPort;
	uint64_t	accountId;
	const char*	sharedDataManifestPath;
	int32_t		headlessMode;
};

struct JAM_ClientPumpOptions
{
	uint32_t	structSize; 
	uint64_t	maxControlEvents;
};

struct JAM_ClientPumpResult
{
	uint32_t	structSize; 
	uint64_t	appliedControlEvents; 
	uint64_t	pendingControlEvents; 
	int32_t		presentationUpdated;
};

struct JAM_NetworkState
{
	JAM_eNetworkPhase phase;
};

struct JAM_WorldRuntimeRef
{
	uint64_t	worldId; 
	uint64_t	worldInstanceId; 
	uint64_t	worldArchetypeKey;
};

struct JAM_ClientRequestReceipt
{
	uint64_t				requestId; 
	JAM_eClientRequestKind	kind;
};

struct JAM_ClientRequestSubmission
{
	JAM_eClientRequestAdmission		admission; 
	JAM_ClientRequestReceipt		receipt;
};

struct JAM_EnterWorldRequest
{
	uint64_t						worldArchetypeKey;
	JAM_eWorldDestinationSelector	selector;
	uint64_t						explicitWorldInstanceId;
	const char*						destinationName;
	uint64_t						expectedMainRevision;
};

struct JAM_LeaveWorldRequest
{
	uint64_t expectedMainRevision;
};

struct JAM_WorldActionCommand
{
	uint32_t				structSize; 
	JAM_eWorldActionKind	kind; 
	JAM_EnterWorldRequest	enter; 
	JAM_LeaveWorldRequest	leave;
};

struct JAM_ActorSpawnSpec
{
	uint64_t	actorArchetypeKey;
	JAM_Vec3	position;
	JAM_Quat	rotation;
	uint16_t	team;
	uint8_t		part;
	uint8_t		role;
	int32_t		requestOwnership;
	int32_t		requestControl;
	uint32_t	targetActorId;
	uint32_t	overrideMask;
	JAM_Vec3	linearVelocity;
	JAM_Vec3	angularVelocity;
	float		linearDamping;
	float		angularDamping;
	float		viewYaw;
	float		viewPitch;
};

struct JAM_ActorActionCommand
{
	uint32_t			structSize; 
	JAM_eActorAction	action; 
	uint64_t			worldId; 
	JAM_ActorSpawnSpec	spawn; 
	uint32_t			targetActorId;
};

struct JAM_SocialAddress
{
	JAM_eSocialAudience	audience;
	uint64_t			scopeId;
};

struct JAM_SocialCommand
{
	uint32_t			structSize;
	JAM_SocialAddress	destination;
	uint16_t			contentType;
	const uint8_t*		payload;
	uint32_t			payloadSize;
};

struct JAM_CharacterControlIntent
{
	uint32_t						structSize;
	float							moveReferenceYaw;
	float							viewYaw;
	float							viewPitch;
	JAM_eCharacterViewPolicy		viewPolicy;
	uint32_t						continuousActions;
	uint32_t						edgeActions;
	JAM_eCharacterLocomotionKind	locomotion;
	JAM_Vec3						vector;
	JAM_Vec3						rayOrigin;
	JAM_Vec3						rayDirection;
	float							maxRange;
	uint32_t						targetActorId;
};

struct JAM_ActorState
{
	uint32_t	actorId; 
	int32_t		isLocal; 
	int32_t		hasTransform; 
	JAM_Vec3	position; 
	JAM_Quat	rotation;
};

struct JAM_ActorFrame
{
	uint64_t	sequence; 
	uint32_t	tick; 
	float		timestamp; 
	int32_t		actorCount;
};

struct JAM_FrameCopyInfo
{
	uint32_t	structSize; 
	int32_t		previousRequiredCount; 
	int32_t		previousCopiedCount; 
	int32_t		currentRequiredCount; 
	int32_t		currentCopiedCount;
};

struct JAM_NetworkStateChangedEvent
{
	uint64_t			accountId; 
	uint64_t			userId; 
	JAM_NetworkState	state;
};

struct JAM_WorldParticipantChangedEvent
{
	uint64_t					accountId; 
	uint64_t					userId; 
	JAM_eWorldParticipantChange	change; 
	JAM_WorldRuntimeRef			world; 
	uint64_t					participantUserId;
};

struct JAM_ActorLifecycleChangedEvent
{
	uint64_t					accountId; 
	uint64_t					userId; 
	uint64_t					worldId; 
	uint64_t					clientRequestId; 
	uint32_t					actorId; 
	int32_t						isLocal; 
	JAM_eActorLifecycleReason	reason; 
	uint64_t					actorArchetypeKey;
};

struct JAM_WorldRayResolvedEvent
{
	uint64_t	accountId;
	uint64_t	userId;
	uint64_t	worldId;
	int32_t		hit;
	JAM_Vec3	position;
	JAM_Vec3	normal;
	uint32_t	hitActorId;
};

struct JAM_ActorActionRequestCompletedEvent
{
	JAM_ClientRequestReceipt	receipt; 
	JAM_eActorAction			action; 
	JAM_eActorActionStatus		status; 
	JAM_eActorActionReason		reason; 
	uint32_t					actorId;
};

struct JAM_SocialMessageReceivedEvent
{
	uint64_t			accountId;
	uint64_t			userId;
	uint64_t			messageId;
	uint64_t			senderUserId;
	JAM_SocialAddress	destination;
	uint16_t			contentType;
	const uint8_t*		payload;
	uint32_t			payloadSize;
};

union JAM_ClientEventPayload
{
	JAM_NetworkStateChangedEvent			networkStateChanged;
	JAM_WorldParticipantChangedEvent		worldParticipantChanged;
	JAM_ActorLifecycleChangedEvent			actorLifecycleChanged;
	JAM_WorldRayResolvedEvent				worldRayResolved;
	JAM_ActorActionRequestCompletedEvent	actorActionRequestCompleted;
	JAM_SocialMessageReceivedEvent			socialMessageReceived;
};

struct JAM_ClientEvent
{
	uint32_t				structSize; 
	JAM_eClientEventType	type; 
	JAM_ClientEventPayload	payload;
};

JAM_API JAM_eResult		JU_Initialize(const JAM_ClientConfig* config);
JAM_API void			JU_Shutdown();
JAM_API JAM_eResult		JU_Connect();
JAM_API void			JU_Disconnect();
JAM_API JAM_eResult		JU_Pump(const JAM_ClientPumpOptions* options, JAM_ClientPumpResult* outResult);
JAM_API uint32_t		JU_GetAbiVersion();
JAM_API JAM_eResult		JU_GetNetworkState(JAM_NetworkState* outState);
JAM_API JAM_eResult		JU_GetAccountId(uint64_t* outAccountId);
JAM_API JAM_eResult		JU_GetUserId(uint64_t* outUserId);
JAM_API JAM_eResult		JU_GetMainWorldRef(JAM_WorldRuntimeRef* outWorldRef);
JAM_API JAM_eResult		JU_GetActorPresentationFramePair(uint64_t worldId, JAM_ActorState* outPreviousActors, int32_t previousCapacity, JAM_ActorFrame* outPreviousFrame, JAM_ActorState* outCurrentActors, int32_t currentCapacity, JAM_ActorFrame* outCurrentFrame, JAM_FrameCopyInfo* outInfo);
JAM_API JAM_eResult		JU_RequestWorldAction(const JAM_WorldActionCommand* command, JAM_ClientRequestSubmission* outSubmission);
JAM_API JAM_eResult		JU_RequestActorAction(const JAM_ActorActionCommand* command, JAM_ClientRequestSubmission* outSubmission);
JAM_API JAM_eResult		JU_RequestSocialCommand(const JAM_SocialCommand* command, JAM_ClientRequestSubmission* outSubmission);
JAM_API JAM_eResult		JU_SubmitCharacterControl(const JAM_CharacterControlIntent* intent);
// Social event payload remains valid until the next JU_PollEvent call or JU_Shutdown.
JAM_API JAM_eResult		JU_PollEvent(JAM_ClientEvent* outEvent);
