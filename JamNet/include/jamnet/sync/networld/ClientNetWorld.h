#pragma once
#include "NetWorld.h"
#include "jamnet/sync/replication/ReplicationEvents.h"
#include "jamnet/sync/replication/ReplicationTypes.h"
#include "jamnet/sync/transport/ITransportEndpoint.h"
#include "jamnet/sync/schema/gen/actor_spawn_generated.h"
#include "jamnet/sync/schema/gen/actor_control_generated.h"

#include <jampx/IPhysicsFacade.h>

#include "jamnet/sync/replication/NetActorComponents.h"

namespace jam::net
{
	class ClientNetWorld : public NetWorld
	{
	public:
		ClientNetWorld() = default;
		~ClientNetWorld() override = default;

		void								Init() override;

		void								SetTransportSystem(shared_ptr<ITransportEndpoint> transport);
		void								SetPhysicsFacade(unique_ptr<px::IPhysicsFacade> physics);
		void								SetLevelPath(const string& levelPath) { m_levelPath = levelPath; }


		void								SetUserId(uint64 userId) { m_userId = userId; }
		uint64								GetUserId() const { return m_userId; }

		entt::entity						GetEntity(NetId netId);

		void								Send(const shared_ptr<SendBuffer>& buf);
		void								OnRecvPacket(const PacketView& view);

		void								SpawnActor(SpawnParams params);
		void								DespawnActor(NetId netId);

		void								PushInput(uint32 inputFlags, float facingYaw, float facingPitch);


		void								RequestDespawnActor(NetId netId);
		void								RequestPossessActor(NetId netId);
		void								RequestUnpossessActor(NetId netId);

		entt::entity						EnsureReplicatedActor(NetId netId, px::PrefabKey prefabKey, uint64 owner, uint64 controller);
		entt::entity						TryConfirmPendingSpawn(NetId netId, uint32 spawnReqId);
	

	private:
		void								TickOnShard() override;
		
		void								ProcessSnapshot(const PacketView& view);

		void								SpawnActorImpl(SpawnParams params);
		void								DespawnActorImpl(NetId netId);

		void								RequestSpawnActor(const SpawnParams& params);

		void								OnSpawnActorResponse(optional<fb::fbSpawnActorResT> res);
		void								OnDespawnActorResponse(optional<fb::fbDespawnActorResT> res);
		void								OnPossessActorResponse(optional<fb::fbPossessActorResT> res);
		void								OnUnpossesActorResponse(optional<fb::fbUnpossessActorResT> res);

		void								BootstrapLevelActors();

	private:
		shared_ptr<ITransportEndpoint>			m_transport;
		
		unique_ptr<px::IPhysicsFacade>			m_physics;

		string									m_levelPath;
		px::LevelLayerInfo						m_levelLayerInfo = {};

		uint64									m_userId = 0;
		NetId									m_localNetId = NetId::Invalid();

		unordered_map<NetId, entt::entity>		m_netIdToEntity;		// netId -> entity (for ensure by server)
		unordered_map<uint32, entt::entity>		m_spawnReqIdToEntity;	// spawnReqId -> pending entities
	};
}
