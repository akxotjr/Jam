#include "pch.h"
#include "jamnet/runtime/protocol/codec/ActorCodec.h"

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/runtime/protocol/codec/RuntimePacketCodec.h"
#include "jamnet/runtime/world/simulation/server/ServerWorld.h"

namespace jam::net::codec
{
	namespace
	{
		struct SpawnWireValues
		{
			fb::fbVec3 pos;
			fb::fbQuat rot;
			fb::fbVec3 linearVelocity;
			fb::fbVec3 angularVelocity;
			const fb::fbVec3* linearVelocityPtr = nullptr;
			const fb::fbVec3* angularVelocityPtr = nullptr;
			uint32 overrideMask = 0;
			float linearDamping = 0.0f;
			float angularDamping = 0.0f;
			float yaw = 0.0f;
			float pitch = 0.0f;

			explicit SpawnWireValues(const SpawnParams& params)
				: pos(params.desc.pose.p.x, params.desc.pose.p.y, params.desc.pose.p.z)
				, rot(params.desc.pose.q.x, params.desc.pose.q.y, params.desc.pose.q.z, params.desc.pose.q.w)
			{
				if (params.desc.IsRigid())
				{
					const auto& overrides = std::get<px::RigidSpawnOverrides>(params.desc.overrides);
					overrideMask = overrides.mask.bits();
					if (overrides.mask.has_any(px::SpawnOverrideMask::LINEAR_VEL))
					{
						linearVelocity = { overrides.linearVelocity.x, overrides.linearVelocity.y, overrides.linearVelocity.z };
						linearVelocityPtr = &linearVelocity;
					}
					if (overrides.mask.has_any(px::SpawnOverrideMask::ANGULAR_VEL))
					{
						angularVelocity = { overrides.angularVelocity.x, overrides.angularVelocity.y, overrides.angularVelocity.z };
						angularVelocityPtr = &angularVelocity;
					}
					if (overrides.mask.has_any(px::SpawnOverrideMask::LINEAR_DAMP)) linearDamping = overrides.linearDamping;
					if (overrides.mask.has_any(px::SpawnOverrideMask::ANGULAR_DAMP)) angularDamping = overrides.angularDamping;
				}
				else
				{
					const auto& overrides = std::get<px::CharacterSpawnOverrides>(params.desc.overrides);
					overrideMask = overrides.mask.bits();
					if (overrides.mask.has_any(px::SpawnOverrideMask::VIEW_YAW)) yaw = overrides.yaw;
					if (overrides.mask.has_any(px::SpawnOverrideMask::VIEW_PITCH)) pitch = overrides.pitch;
				}
			}
		};

		template<typename Wire>
		bool DecodeSpawn(const Wire& wire, SpawnParams& out)
		{
			if (!wire.pos() || !wire.rot())
				return false;

			SpawnParams decoded{};
			decoded.clientRequestId = wire.client_request_id();
			decoded.actorArchetypeKey = ActorArchetypeKey::FromU64(wire.actor_archetype_key());
			decoded.targetActorId = ActorId(wire.target_actor_id());
			decoded.desc.spawnSrc = static_cast<px::eSpawnSource>(wire.spawn_src());
			decoded.desc.pose = {
				.p = { wire.pos()->x(), wire.pos()->y(), wire.pos()->z() },
				.q = { wire.rot()->x(), wire.rot()->y(), wire.rot()->z(), wire.rot()->w() },
			};
			px::SpawnOverrideMask::Flag mask{ wire.override_mask() };
			if (px::IsRigidOverrideMask(mask))
			{
				px::RigidSpawnOverrides overrides{};
				overrides.mask = mask;
				if (mask.has_any(px::SpawnOverrideMask::LINEAR_VEL) && wire.linear_vel())
					overrides.linearVelocity = { wire.linear_vel()->x(), wire.linear_vel()->y(), wire.linear_vel()->z() };
				if (mask.has_any(px::SpawnOverrideMask::ANGULAR_VEL) && wire.angular_vel())
					overrides.angularVelocity = { wire.angular_vel()->x(), wire.angular_vel()->y(), wire.angular_vel()->z() };
				if (mask.has_any(px::SpawnOverrideMask::LINEAR_DAMP)) overrides.linearDamping = wire.linear_damping();
				if (mask.has_any(px::SpawnOverrideMask::ANGULAR_DAMP)) overrides.angularDamping = wire.angular_damping();
				decoded.desc.overrides = overrides;
			}
			else
			{
				px::CharacterSpawnOverrides overrides{};
				overrides.mask = mask;
				if (mask.has_any(px::SpawnOverrideMask::VIEW_YAW)) overrides.yaw = wire.yaw();
				if (mask.has_any(px::SpawnOverrideMask::VIEW_PITCH)) overrides.pitch = wire.pitch();
				decoded.desc.overrides = overrides;
			}

			out = std::move(decoded);
			return true;
		}

		FlatBufferPayload Finish(flatbuffers::FlatBufferBuilder& fbb)
		{
			return { fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()) };
		}
	}

	FlatBufferPayload EncodeSpawnActorRequest(const WorldRef& world, const SpawnParams& params)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		const SpawnWireValues values(params);
		const auto root = fb::CreatefbSpawnActorReq(fbb, world.worldId, world.instance.instanceId.value,
			params.clientRequestId, params.owner, params.controller, params.actorArchetypeKey.v, &values.pos, &values.rot,
			static_cast<uint32>(params.desc.spawnSrc),
			values.overrideMask, values.linearVelocityPtr, values.angularVelocityPtr, values.linearDamping,
			values.angularDamping, values.yaw, values.pitch, params.targetActorId.Value());
		fbb.Finish(root);
		return Finish(fbb);
	}

	FlatBufferPayload EncodeSpawnPlayerRequest(const WorldEventCorrelation& correlation, const SpawnParams& params)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		const SpawnWireValues values(params);
		const auto root = fb::CreatefbSpawnPlayerReq(fbb, correlation.world.worldId,
			correlation.world.instance.instanceId.value, correlation.mainRevision, params.clientRequestId,
			params.actorArchetypeKey.v, &values.pos, &values.rot, static_cast<uint32>(params.desc.spawnSrc),
			values.overrideMask, values.linearVelocityPtr,
			values.angularVelocityPtr, values.linearDamping, values.angularDamping, values.yaw, values.pitch,
			params.targetActorId.Value());
		fbb.Finish(root);
		return Finish(fbb);
	}

	FlatBufferPayload EncodeDespawnActorRequest(const WorldRef& world, ActorId actorId)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		fbb.Finish(fb::CreatefbDespawnActorReq(fbb, world.worldId, world.instance.instanceId.value, actorId.Value()));
		return Finish(fbb);
	}

	FlatBufferPayload EncodeDespawnPlayerRequest(const WorldEventCorrelation& correlation, ActorId actorId)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		fbb.Finish(fb::CreatefbDespawnPlayerReq(fbb, correlation.world.worldId,
			correlation.world.instance.instanceId.value, correlation.mainRevision, actorId.Value()));
		return Finish(fbb);
	}

	FlatBufferPayload EncodeSpawnActorResponse(bool success, fb::fbSpawnActorFailure failure, ClientRequestId clientRequestId, ActorId actorId)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		fbb.Finish(fb::CreatefbSpawnActorRes(fbb, success, failure, clientRequestId, actorId.Value()));
		return Finish(fbb);
	}

	FlatBufferPayload EncodeSpawnPlayerResponse(bool success, fb::fbSpawnPlayerFailure failure, ClientRequestId clientRequestId, ActorId actorId)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		fbb.Finish(fb::CreatefbSpawnPlayerRes(fbb, success, failure, clientRequestId, actorId.Value()));
		return Finish(fbb);
	}

	FlatBufferPayload EncodeDespawnActorResponse(bool success)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		fbb.Finish(fb::CreatefbDespawnActorRes(fbb, success));
		return Finish(fbb);
	}

	FlatBufferPayload EncodeDespawnPlayerResponse(bool success)
	{
		auto& fbb = ResetRuntimePacketBuilder(CurrentShardLocalChecked());
		fbb.Finish(fb::CreatefbDespawnPlayerRes(fbb, success));
		return Finish(fbb);
	}

	bool DecodeSpawnActorRequest(const fb::fbSpawnActorReq& wire, SpawnParams& out)
	{
		if (!DecodeSpawn(wire, out)) return false;
		out.owner = wire.owner_user_id();
		out.controller = wire.controller_user_id();
		return true;
	}

	bool DecodeSpawnPlayerRequest(const fb::fbSpawnPlayerReq& wire, UserId userId, SpawnParams& out)
	{
		if (!DecodeSpawn(wire, out)) return false;
		out.owner = userId;
		out.controller = userId;
		return true;
	}

	ActorActionResult DecodeSpawnActorResponse(const fb::fbSpawnActorRes& wire)
	{
		return {
			.status = wire.success() ? eActorActionStatus::Succeeded : eActorActionStatus::Failed,
			.reason = wire.success() ? eActorActionReason::None
				: ((wire.failure() == fb::fbSpawnActorFailure_InvalidCorrelation || wire.failure() == fb::fbSpawnActorFailure_StaleRevision)
					? eActorActionReason::WorldUnavailable : eActorActionReason::Rejected),
			.action = eActorAction::Spawn,
			.actorId = ActorId(wire.actor_id()),
		};
	}

	ActorActionResult DecodeSpawnPlayerResponse(const fb::fbSpawnPlayerRes& wire)
	{
		return {
			.status = wire.success() ? eActorActionStatus::Succeeded : eActorActionStatus::Failed,
			.reason = wire.success() ? eActorActionReason::None
				: ((wire.failure() == fb::fbSpawnPlayerFailure_InvalidCorrelation || wire.failure() == fb::fbSpawnPlayerFailure_StaleRevision)
					? eActorActionReason::WorldUnavailable : eActorActionReason::Rejected),
			.action = eActorAction::Spawn,
			.actorId = ActorId(wire.actor_id()),
		};
	}

	ActorActionResult DecodeDespawnActorResponse(const fb::fbDespawnActorRes& wire, ActorId actorId)
	{
		return {
			.status = wire.success() ? eActorActionStatus::Succeeded : eActorActionStatus::Failed,
			.reason = wire.success() ? eActorActionReason::None : eActorActionReason::Rejected,
			.action = eActorAction::Despawn,
			.actorId = actorId,
		};
	}

	ActorActionResult DecodeDespawnPlayerResponse(const fb::fbDespawnPlayerRes& wire, ActorId actorId)
	{
		return {
			.status = wire.success() ? eActorActionStatus::Succeeded : eActorActionStatus::Failed,
			.reason = wire.success() ? eActorActionReason::None : eActorActionReason::Rejected,
			.action = eActorAction::Despawn,
			.actorId = actorId,
		};
	}

	fb::fbSpawnActorFailure EncodeSpawnActorFailure(ePlayerSpawnFailure failure)
	{
		switch (failure)
		{
		case ePlayerSpawnFailure::None: return fb::fbSpawnActorFailure_None;
		case ePlayerSpawnFailure::InvalidCorrelation: return fb::fbSpawnActorFailure_InvalidCorrelation;
		case ePlayerSpawnFailure::AlreadySpawned: return fb::fbSpawnActorFailure_AlreadySpawned;
		case ePlayerSpawnFailure::SpawnFailed: return fb::fbSpawnActorFailure_SpawnFailed;
		}
		return fb::fbSpawnActorFailure_SpawnFailed;
	}

	fb::fbSpawnPlayerFailure EncodeSpawnPlayerFailure(ePlayerSpawnFailure failure)
	{
		switch (failure)
		{
		case ePlayerSpawnFailure::None: return fb::fbSpawnPlayerFailure_None;
		case ePlayerSpawnFailure::InvalidCorrelation: return fb::fbSpawnPlayerFailure_InvalidCorrelation;
		case ePlayerSpawnFailure::AlreadySpawned: return fb::fbSpawnPlayerFailure_AlreadySpawned;
		case ePlayerSpawnFailure::SpawnFailed: return fb::fbSpawnPlayerFailure_SpawnFailed;
		}
		return fb::fbSpawnPlayerFailure_SpawnFailed;
	}
}
