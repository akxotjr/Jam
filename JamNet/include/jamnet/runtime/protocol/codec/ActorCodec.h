#pragma once

#include "jamnet/runtime/protocol/schema/gen/actor_spawn_generated.h"
#include "jamnet/runtime/world/actor/ActorActionTypes.h"
#include "jamnet/runtime/world/simulation/common/PhysicalWorld.h"

namespace jam::net
{
	enum class ePlayerSpawnFailure : uint8;
}

namespace jam::net::codec
{
	struct FlatBufferPayload
	{
		const uint8* data = nullptr;
		uint32		 size = 0;
	};

	FlatBufferPayload EncodeSpawnActorRequest(const WorldRef& world, const SpawnParams& params);
	FlatBufferPayload EncodeSpawnPlayerRequest(const WorldEventCorrelation& correlation, const SpawnParams& params);
	FlatBufferPayload EncodeDespawnActorRequest(const WorldRef& world, ActorId actorId);
	FlatBufferPayload EncodeDespawnPlayerRequest(const WorldEventCorrelation& correlation, ActorId actorId);

	FlatBufferPayload EncodeSpawnActorResponse(bool success, fb::fbSpawnActorFailure failure, ClientRequestId clientRequestId, ActorId actorId);
	FlatBufferPayload EncodeSpawnPlayerResponse(bool success, fb::fbSpawnPlayerFailure failure, ClientRequestId clientRequestId, ActorId actorId);
	FlatBufferPayload EncodeDespawnActorResponse(bool success);
	FlatBufferPayload EncodeDespawnPlayerResponse(bool success);

	bool DecodeSpawnActorRequest(const fb::fbSpawnActorReq& wire, SpawnParams& out);
	bool DecodeSpawnPlayerRequest(const fb::fbSpawnPlayerReq& wire, UserId userId, SpawnParams& out);
	ActorActionResult DecodeSpawnActorResponse(const fb::fbSpawnActorRes& wire);
	ActorActionResult DecodeSpawnPlayerResponse(const fb::fbSpawnPlayerRes& wire);
	ActorActionResult DecodeDespawnActorResponse(const fb::fbDespawnActorRes& wire, ActorId actorId);
	ActorActionResult DecodeDespawnPlayerResponse(const fb::fbDespawnPlayerRes& wire, ActorId actorId);
	fb::fbSpawnActorFailure EncodeSpawnActorFailure(ePlayerSpawnFailure failure);
	fb::fbSpawnPlayerFailure EncodeSpawnPlayerFailure(ePlayerSpawnFailure failure);
}
