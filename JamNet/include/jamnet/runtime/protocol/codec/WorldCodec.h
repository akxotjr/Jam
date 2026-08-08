#pragma once

#include "jamnet/core/net/Buffer.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"

namespace jam::net::codec
{
	Packet MakeEnterWorldRequestPacket(const EnterWorldRequest& request);
	Packet MakeLeaveWorldRequestPacket(const LeaveWorldRequest& request);
	Packet MakeWorldTransitionResultPacket(const WorldTransitionResult& result);
	Packet MakeUserMainWorldChangedPacket(const UserWorldState& state);
	Packet MakeClientWorldPreparePacket(const ClientWorldPrepare& prepare);
	Packet MakeClientWorldSyncResultPacket(const ClientWorldSyncResult& result);
	Packet MakeClientWorldCommitPacket(const ClientWorldCommit& commit);

	bool DecodeEnterWorldRequest(const void* payload, size_t payloadSize, EnterWorldRequest& out);
	bool DecodeLeaveWorldRequest(const void* payload, size_t payloadSize, LeaveWorldRequest& out);
	bool DecodeWorldTransitionResult(const void* payload, size_t payloadSize, WorldTransitionResult& out);
	bool DecodeUserMainWorldChanged(const void* payload, size_t payloadSize, UserWorldState& out);
	bool DecodeClientWorldPrepare(const void* payload, size_t payloadSize, ClientWorldPrepare& out);
	bool DecodeClientWorldSyncResult(const void* payload, size_t payloadSize, ClientWorldSyncResult& out);
	bool DecodeClientWorldCommit(const void* payload, size_t payloadSize, ClientWorldCommit& out);
}
