#include "pch.h"
#include "jamnet/runtime/protocol/codec/WorldCodec.h"

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/runtime/protocol/codec/RuntimePacketCodec.h"
#include "jamnet/runtime/protocol/schema/gen/world_assignment_generated.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"

namespace jam::net::codec
{
	namespace
	{
		template<typename Wire>
		const Wire* Verify(const void* payload, size_t payloadSize)
		{
			if (!payload || payloadSize == 0)
				return nullptr;

			flatbuffers::Verifier verifier(static_cast<const uint8*>(payload), payloadSize);
			const auto* wire = flatbuffers::GetRoot<Wire>(payload);
			
			return wire && wire->Verify(verifier) ? wire : nullptr;
		}

		flatbuffers::Offset<fb::fbUserMainWorldState> EncodeUserWorldState(flatbuffers::FlatBufferBuilder& fbb, const UserWorldState& state)
		{
			flatbuffers::Offset<fb::fbMainWorldRef> main;
			if (state.main)
				main = fb::CreatefbMainWorldRef(fbb, state.main->instance.instanceId.value, state.main->instance.archetypeKey.v, state.main->worldId);
		
			return fb::CreatefbUserMainWorldState(fbb, main, state.revision);
		}

		bool DecodeUserWorldState(const fb::fbUserMainWorldState* wire, UserWorldState& out)
		{
			if (!wire) return false;

			UserWorldState decoded{ .revision = wire->revision() };
			if (const auto* main = wire->main())
			{
				decoded.main = WorldRef{
					.instance = { 
						.instanceId = WorldInstanceId{ main->instance_id() }, 
						.archetypeKey = WorldArchetypeKey{ main->archetype_key() } 
					},
					.worldId = main->world_id(),
				};
			}

			out = decoded;
			return true;
		}

		template<typename Wire>
		Packet FinishPacket(flatbuffers::FlatBufferBuilder& fbb, flatbuffers::Offset<Wire> root, uint8 id)
		{
			fbb.Finish(root);
			return PacketBuilder::CreateCustomPacket(id, PacketFlags::NONE, eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()));
		}
	}


	Packet MakeEnterWorldRequestPacket(const EnterWorldRequest& request)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());

		flatbuffers::Offset<flatbuffers::String> destination;
		if (!request.destinationName.empty())
			destination = fbb.CreateString(request.destinationName);

		const auto root = fb::CreatefbEnterWorldRequest(fbb, request.requestId, request.archetypeKey.v, static_cast<fb::fbWorldDestinationSelector>(request.selector), request.explicitInstanceId.value, destination, request.expectedMainRevision);
		return FinishPacket(fbb, root, CustomPacketId::ENTER_WORLD_REQUEST);
	}

	Packet MakeLeaveWorldRequestPacket(const LeaveWorldRequest& request)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		return FinishPacket(fbb, fb::CreatefbLeaveWorldRequest(fbb, request.requestId, request.expectedMainRevision), CustomPacketId::LEAVE_WORLD_REQUEST);
	}

	Packet MakeWorldTransitionResultPacket(const WorldTransitionResult& result)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		const auto state = EncodeUserWorldState(fbb, result.state);
		const auto root = fb::CreatefbWorldTransitionResult(fbb, result.requestId, static_cast<fb::fbWorldTransitionKind>(result.kind), result.transitionToken.value, static_cast<fb::fbWorldTransitionFailure>(result.failure), state);
		return FinishPacket(fbb, root, CustomPacketId::WORLD_TRANSITION_RESULT);
	}

	Packet MakeUserMainWorldChangedPacket(const UserWorldState& state)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		return FinishPacket(fbb, fb::CreatefbUserMainWorldChanged(fbb, EncodeUserWorldState(fbb, state)), CustomPacketId::USER_MAIN_WORLD_CHANGED);
	}

	Packet MakeClientWorldPreparePacket(const ClientWorldPrepare& prepare)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		const auto& world = prepare.correlation.world;
		const auto root = fb::CreatefbClientWorldPrepare(fbb, prepare.token.value, static_cast<fb::fbWorldSyncKind>(prepare.kind), world.instance.instanceId.value, world.instance.archetypeKey.v, world.worldId, prepare.correlation.mainRevision, prepare.contentRevision);
		return FinishPacket(fbb, root, CustomPacketId::CLIENT_WORLD_PREPARE);
	}

	Packet MakeClientWorldSyncResultPacket(const ClientWorldSyncResult& result)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		const auto root = fb::CreatefbClientWorldSyncResult(fbb, result.token.value, result.succeeded, static_cast<fb::fbWorldTransitionFailure>(result.failure));
		return FinishPacket(fbb, root, CustomPacketId::CLIENT_WORLD_SYNC_RESULT);
	}

	Packet MakeClientWorldCommitPacket(const ClientWorldCommit& commit)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		const auto& world = commit.correlation.world;
		const auto root = fb::CreatefbClientWorldCommit(fbb, commit.token.value, world.instance.instanceId.value, world.worldId, commit.correlation.mainRevision);
		return FinishPacket(fbb, root, CustomPacketId::CLIENT_WORLD_COMMIT);
	}

	bool DecodeEnterWorldRequest(const void* payload, size_t payloadSize, EnterWorldRequest& out)
	{
		const auto* wire = Verify<fb::fbEnterWorldRequest>(payload, payloadSize);
		if (!wire || wire->selector() < fb::fbWorldDestinationSelector_MIN || wire->selector() > fb::fbWorldDestinationSelector_MAX)
			return false;

		out = {
			.requestId = wire->request_id(), 
			.archetypeKey = WorldArchetypeKey{ wire->archetype_key() },
			.selector = static_cast<eWorldDestinationSelector>(wire->selector()),
			.explicitInstanceId = WorldInstanceId{ wire->explicit_instance_id() },
			.destinationName = wire->destination_name() ? wire->destination_name()->str() : std::string{},
			.expectedMainRevision = wire->expected_main_revision(),
		};
		return true;
	}

	bool DecodeLeaveWorldRequest(const void* payload, size_t payloadSize, LeaveWorldRequest& out)
	{
		const auto* wire = Verify<fb::fbLeaveWorldRequest>(payload, payloadSize);
		if (!wire) return false;
		out = {
			.requestId = wire->request_id(), 
			.expectedMainRevision = wire->expected_main_revision()
		};
		return true;
	}

	bool DecodeWorldTransitionResult(const void* payload, size_t payloadSize, WorldTransitionResult& out)
	{
		const auto* wire = Verify<fb::fbWorldTransitionResult>(payload, payloadSize);

		if (!wire || wire->kind() < fb::fbWorldTransitionKind_MIN 
				  || wire->kind() > fb::fbWorldTransitionKind_MAX
				  || wire->failure() < fb::fbWorldTransitionFailure_MIN 
				  || wire->failure() > fb::fbWorldTransitionFailure_MAX)
			return false;

		WorldTransitionResult decoded{
			.kind = static_cast<eWorldTransitionKind>(wire->kind()), 
			.requestId = wire->request_id(),
			.transitionToken = { wire->transition_token() },
			.failure = static_cast<eWorldTransitionFailure>(wire->failure()),
		};

		if (!DecodeUserWorldState(wire->state(), decoded.state)) 
			return false;
		
		out = decoded;
		return true;
	}

	bool DecodeUserMainWorldChanged(const void* payload, size_t payloadSize, UserWorldState& out)
	{
		const auto* wire = Verify<fb::fbUserMainWorldChanged>(payload, payloadSize);
		return wire && DecodeUserWorldState(wire->state(), out);
	}

	bool DecodeClientWorldPrepare(const void* payload, size_t payloadSize, ClientWorldPrepare& out)
	{
		const auto* wire = Verify<fb::fbClientWorldPrepare>(payload, payloadSize);
		if (!wire || wire->kind() < fb::fbWorldSyncKind_MIN || wire->kind() > fb::fbWorldSyncKind_MAX)
			return false;

		out = {
			.token = { wire->sync_token() }, 
			.kind = static_cast<eWorldSyncKind>(wire->kind()),
			.correlation = { 
				.world = { 
					.instance = { 
						.instanceId = WorldInstanceId{ wire->instance_id() }, 
						.archetypeKey = WorldArchetypeKey{ wire->archetype_key() } 
					}, 
					.worldId = wire->world_id() 
				}, 
				.mainRevision = wire->main_revision() 
			},
			.archetypeKey = WorldArchetypeKey{ wire->archetype_key() }, 
			.contentRevision = wire->content_revision(),
		};
		return true;
	}

	bool DecodeClientWorldSyncResult(const void* payload, size_t payloadSize, ClientWorldSyncResult& out)
	{
		const auto* wire = Verify<fb::fbClientWorldSyncResult>(payload, payloadSize);
		if (!wire || wire->failure() < fb::fbWorldTransitionFailure_MIN || wire->failure() > fb::fbWorldTransitionFailure_MAX)
			return false;
		
		out = {
			.token = { wire->sync_token() }, 
			.succeeded = wire->succeeded(), 
			.failure = static_cast<eWorldTransitionFailure>(wire->failure())
		};

		return true;
	}

	bool DecodeClientWorldCommit(const void* payload, size_t payloadSize, ClientWorldCommit& out)
	{
		const auto* wire = Verify<fb::fbClientWorldCommit>(payload, payloadSize);
		if (!wire) return false;

		out = {
			.token = { wire->sync_token() }, 
			.correlation = { 
				.world = { 
					.instance = { .instanceId = WorldInstanceId{ wire->instance_id() } }, 
					.worldId = wire->world_id() 
				},
				.mainRevision = wire->main_revision() 
			}
		};
		return true;
	}
	
}
