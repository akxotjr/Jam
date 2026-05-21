#pragma once

#include "jamnet/runtime/world/WorldActionTypes.h"
#include "jamnet/runtime/world/PhysicalWorld.h"
#include "jamnet/sync/replication/NetActorComponents.h"

namespace jam::net
{
	class IClientNetworkController
	{
	public:
		virtual ~IClientNetworkController() = default;
		
		virtual void RequestWorldAction(eWorldAction action, const WorldKey& src = {}, const WorldKey& target = {}) = 0;
		virtual void RequestSpawnActor(const SpawnParams& params) = 0;
		virtual void RequestDespawnActor(NetId netId) = 0;
		virtual void RequestPossessActor(NetId netId) = 0;
		virtual void RequestUnpossessActor(NetId netId) = 0;
		virtual void PushInput(uint32 inputFlags, float pitch, float yaw, uint32 commandEpoch) = 0;
		virtual void PushInput(const px::CharacterInput& input) = 0;
		virtual void SetLatestClickMoveSeq(uint64 requestSeq) = 0;
		virtual void RequestClickMove(const px::Vec3& from, const px::Vec3& dir, float maxRange, uint64 requestSeq, uint32 commandEpoch, float facingYaw) = 0;
	};
}
