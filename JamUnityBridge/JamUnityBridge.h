#pragma once

#include <cstdint>

#ifdef _WIN32
#define JU_API extern "C" __declspec(dllexport)
#else
#define JU_API extern "C"
#endif

enum JUInputFlag : uint32_t
{
	JU_INPUT_NONE		= 0,
	JU_INPUT_FORWARD	= 1u << 0,
	JU_INPUT_BACKWARD	= 1u << 1,
	JU_INPUT_LEFT		= 1u << 2,
	JU_INPUT_RIGHT		= 1u << 3,
	JU_INPUT_CROUCH		= 1u << 4,
	JU_INPUT_PRONE		= 1u << 5,
	JU_INPUT_RUN		= 1u << 6,
	JU_INPUT_SPRINT		= 1u << 7,
	JU_INPUT_JUMP		= 1u << 8,
	JU_INPUT_DASH		= 1u << 9
};

enum class JUEventType : int32_t
{
	None				= 0,
	NetworkState		= 1,
	WorldMembership		= 2,
	ActorSpawned		= 3,
	ActorDespawned		= 4,
	ClickMoveResolved	= 5
};

enum class JUNetworkPhase : int32_t
{
	Disconnected		= 0,
	Connecting			= 1,
	Ready				= 2,
	Degraded			= 3
};

enum class JUWorldMembershipChange : int32_t
{
	Joined				= 0,
	Left				= 1,
	Promoted			= 2,
	Transferred			= 3,
	Updated				= 4
};

struct JUVec3
{
	float x;
	float y;
	float z;
};

struct JUQuat
{
	float x;
	float y;
	float z;
	float w;
};

struct JUClientConfig
{
	const char*		serverIp;
	uint16_t		tcpPort;
	uint16_t		udpPort;
	uint64_t		accountId;
	uint32_t		instanceId;
	const char*		sharedDataCatalogAssetPath;
	const char*		worldTemplateAssetPath;
	const char*		worldArchetypeAssetPath;
	uint64_t		autoAssignArchetypeKey;
	int32_t			autoAssignOnReady;
	int32_t			headlessWorld;
};

struct JUInputCommand
{
	uint32_t		inputFlags;
	uint32_t		commandEpoch;
	float			facingYaw;
	float			facingPitch;
};

struct JUClickMoveCommand
{
	JUVec3			rayOrigin;
	JUVec3			rayDirection;
	float			maxRange;
	uint64_t		requestSeq;
	uint32_t		commandEpoch;
	float			facingYaw;
};

struct JUNetworkStateEvent
{
	JUNetworkPhase	phase;
	uint64_t		accountId;
	uint64_t		userId;
};

struct JUWorldMembershipEvent
{
	JUWorldMembershipChange change;
	uint64_t				localWorldId;
	uint64_t				worldId;
	uint64_t				archetypeKey;
};

struct JUActorSpawnedEvent
{
	uint32_t		objectId;
	uint64_t		actorArchetypeKey;
	int32_t			isLevelActor;
	int32_t			isLocal;
};

struct JUActorDespawnedEvent
{
	uint32_t		objectId;
};

struct JUClickMoveResolvedEvent
{
	uint64_t requestSeq;
	int32_t followTarget;
	JUVec3 targetPos;
};

struct JUActorState
{
	uint32_t objectId;
	int32_t isLevelActor;
	int32_t isLocal;
	int32_t hasTransform;
	JUVec3 position;
	JUQuat rotation;
};

struct JUActorFrame
{
	uint64_t sequence;
	uint32_t tick;
	float timestamp;
	int32_t actorCount;
};

JU_API bool				JU_Initialize(const JUClientConfig* config);
JU_API void				JU_Shutdown();
JU_API void				JU_Pump(float deltaTime);

JU_API bool				JU_IsConnected();
JU_API JUNetworkPhase	JU_GetNetworkPhase();
JU_API uint64_t			JU_GetUserId();
JU_API uint64_t			JU_GetMainLocalWorldId();
JU_API uint64_t			JU_GetMainWorldArchetypeKey();

JU_API void				JU_SubmitInput(const JUInputCommand* command);
JU_API void				JU_RequestClickMove(const JUClickMoveCommand* command);

JU_API int32_t			JU_CopyActorFrame(JUActorState* outActors, int32_t actorCapacity, JUActorFrame* outFrame);

JU_API JUEventType		JU_PopEventType();
JU_API bool				JU_ReadNetworkStateEvent(JUNetworkStateEvent* outEvent);
JU_API bool				JU_ReadWorldMembershipEvent(JUWorldMembershipEvent* outEvent);
JU_API bool				JU_ReadActorSpawnedEvent(JUActorSpawnedEvent* outEvent);
JU_API bool				JU_ReadActorDespawnedEvent(JUActorDespawnedEvent* outEvent);
JU_API bool				JU_ReadClickMoveResolvedEvent(JUClickMoveResolvedEvent* outEvent);
