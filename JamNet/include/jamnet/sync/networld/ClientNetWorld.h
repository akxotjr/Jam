#pragma once
#include "jamnet/sync/networld/NetWorld.h"
#include "jamnet/sync/replication/ReplicationEvents.h"
#include "jamnet/sync/transport/ITransportEndpoint.h"
#include "jamnet/sync/replication/NetActorComponents.h"

#include "jamnet/sync/schema/gen/actor_spawn_generated.h"
#include "jamnet/sync/schema/gen/actor_control_generated.h"

#include <jampx/IPhysicsFacade.h>
#include <atomic>
#include <functional>



namespace jam::net
{
	class ClientNetWorld : public NetWorld
	{
	public:
		ClientNetWorld() = default;
		~ClientNetWorld() override = default;

		void								Init() override;

		void								SetTransportSystem(std::shared_ptr<ITransportEndpoint> transport);
		void								SetPhysicsFacade(std::unique_ptr<px::IPhysicsFacade> physics);
		void								SetLevelPath(const std::string& levelPath) { m_levelPath = levelPath; }


		void								SetUserId(uint64 userId) { m_userId = userId; }
		uint64								GetUserId() const { return m_userId; }

		entt::entity						GetEntity(NetId netId);

		void								Send(const std::shared_ptr<SendBuffer>& buf);
		void								OnRecvPacket(const PacketView& view);

		void								SpawnActor(SpawnParams params);
		void								DespawnActor(NetId netId);

		void								PushInput(uint32 inputFlags, float facingYaw, float facingPitch, uint32 commandEpoch = 0);
		void								PushInput(const px::CharacterInput& input);
		uint32								GetLatestLocalCommandEpoch() const { return m_latestLocalCommandEpoch.load(std::memory_order_acquire); }
		void								SetLatestClickMoveSeq(uint64 requestSeq);
		void								RequestClickMove(const px::Vec3& from, const px::Vec3& dir, float maxRange, uint64 requestSeq, uint32 commandEpoch, float facingYaw);
		void								RequestHitscan(const px::Vec3& from, const px::Vec3& dir, float maxRange, std::function<void(const px::HitscanResult&)> onDone);
		bool								TryGetNetIdFromObjectId(px::ObjectId objectId, OUT NetId& outNetId);


		void								RequestDespawnActor(NetId netId);
		void								RequestPossessActor(NetId netId);
		void								RequestUnpossessActor(NetId netId);
		void								SetReplicatedActorDormant(NetId netId);
		void								PredictReplicatedActorDespawn(NetId netId);
		void								ReactivateReplicatedActor(NetId netId, bool isLocal);
		void								DestroyReplicatedActor(NetId netId);

		entt::entity						EnsureReplicatedActor(NetId netId, px::PrefabKey prefabKey, uint64 owner, uint64 controller);
		entt::entity						TryConfirmPendingSpawn(NetId netId, uint32 spawnReqId);
	

	private:
		void								TickOnShard() override;
		
		void								ProcessSnapshot(const PacketView& view);

		void								SpawnActorImpl(SpawnParams params);
		void								DespawnActorImpl(NetId netId);
		void								SetActorDormantImpl(NetId netId);
		void								PublishActorSpawned(entt::entity e, uint32 spawnReqId, bool isLocal);
		void								PublishActorDespawned(entt::entity e);

		void								RequestSpawnActor(const SpawnParams& params);

		void								OnSpawnActorResponse(std::optional<fb::fbSpawnActorResT> res);
		void								OnDespawnActorResponse(std::optional<fb::fbDespawnActorResT> res);
		void								OnPossessActorResponse(std::optional<fb::fbPossessActorResT> res);
		void								OnUnpossesActorResponse(std::optional<fb::fbUnpossessActorResT> res);

		void								BootstrapLevelActors();

	private:
		std::shared_ptr<ITransportEndpoint>				m_transport;

		std::unique_ptr<px::IPhysicsFacade>				m_physics;

		std::string										m_levelPath;
		px::LevelLayerInfo								m_levelLayerInfo = {};

		uint64											m_userId = 0;
		NetId											m_localNetId = NetId::Invalid();
		std::atomic<uint64>								m_latestClickMoveSeq{ 0 };
		std::atomic<uint32>								m_latestLocalCommandEpoch{ 0 };

		std::unordered_map<NetId, entt::entity>			m_netIdToEntity;		// netId -> entity (for ensure by server)
		std::unordered_map<uint32, entt::entity>		m_spawnReqIdToEntity;	// spawnReqId -> pending entities
	};
}
