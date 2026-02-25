#pragma once
#include "NetWorld.h"
#include "jamnet/sync/replication/ReplicationEvents.h"
#include "jamnet/sync/replication/ReplicationTypes.h"
#include "jamnet/sync/transport/ITransportEndpoint.h"
#include "jamnet/sync/schema/gen/actor_spawn_generated.h"
#include "jamnet/sync/schema/gen/actor_control_generated.h"

#include "IPhysicsFacade.h"


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

		void								SetUserId(uint64 userId) { m_userId = userId; }
		uint64								GetUserId() const { return m_userId; }

		entt::entity						GetEntity(uint32 netId);

		void								Send(const shared_ptr<SendBuffer>& buf);
		void								OnRecvPacket(const PacketView& view);

		void								SpawnActor(const SpawnParams& params);
		void								DespawnActor(uint32 netId);

		void								PushInput(uint32 inputFlags, float facingYaw, float facingPitch);


		void								RequestDespawnActor(uint32 netId);
		void								RequestPossessActor(uint32 netId);
		void								RequestUnpossessActor(uint32 netId);

		entt::entity						EnsureReplicatedActor(uint32 netId, px::PrefabKey prefabKey, uint64 owner, uint64 controller);
		entt::entity						TryConfirmPendingSpawn(uint32 spawnReqId, uint32 netId);
	
		void								SetLevelPath(const string& levelPath) { m_levelPath = levelPath; }

	private:
		void								TickOnShard() override;
		
		void								ProcessSnapshot(const PacketView& view);

		void								SpawnActorImpl(const SpawnParams& params);
		void								DespawnActorImpl(uint32 netId);

		void								RequestSpawnActor(const SpawnParams& params);

		void								OnSpawnActorResponse(optional<fb::fbSpawnActorResT> res);
		void								OnDespawnActorResponse(optional<fb::fbDespawnActorResT> res);
		void								OnPossessActorResponse(optional<fb::fbPossessActorResT> res);
		void								OnUnpossesActorResponse(optional<fb::fbUnpossessActorResT> res);


	private:
		shared_ptr<ITransportEndpoint>			m_transport;
		
		unique_ptr<px::IPhysicsFacade>			m_physics;

		string									m_levelPath;

		uint64									m_userId = 0;
		uint32									m_localNetId = 0;

		unordered_map<uint32, entt::entity>		m_netIdToEntity;		// netId -> entity (for ensure by server)
		unordered_map<uint32, entt::entity>		m_spawnReqIdToEntity;	// spawnReqId -> pending entities
	};
}