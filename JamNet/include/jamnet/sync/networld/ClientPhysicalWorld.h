#pragma once
#include "jamnet/runtime/world/PhysicalWorld.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/runtime/AppRuntimeEvents.h"
#include "jamnet/runtime/schema/RPCSchemaIds.h"

#include "jamnet/sync/schema/gen/actor_spawn_generated.h"
#include "jamnet/sync/schema/gen/actor_control_generated.h"

#include <jampx/IPhysicsFacade.h>
#include <atomic>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>

#include "jamnet/runtime/ClientSession.h"


namespace jam::net
{

	class ClientPhysicalWorld : public PhysicalWorld
	{
	public:
		ClientPhysicalWorld() = default;
		explicit ClientPhysicalWorld(const WorldConfig& config) : PhysicalWorld(config) {}
		~ClientPhysicalWorld() override = default;

		bool								Init() override;
		void								Start(uint64 dt_ns) override;
		void								Resume(uint64 dt_ns) override;
		void								Stop() override;
		void								Shutdown(eMailboxCloseMode mode, std::function<void()> onClosed = nullptr) override;

		void								SetPhysicsFacade(std::unique_ptr<px::IPhysicsFacade> physics);
		void								SetLevelPath(const std::string& levelPath) { m_levelPath = levelPath; }
		void								SetHeadless(bool headless) { m_headless = headless; }
		bool								IsHeadless() const { return m_headless; }

		void								SetSessionBundle(const ClientSessionBundle& sessions);

		void								SetAccountId(uint64 accountId) { m_accountId = accountId; }
		uint64								GetAccountId() const { return m_accountId; }
		void								SetUserId(uint64 userId) { m_userId = userId; }
		uint64								GetUserId() const { return m_userId; }
		bool								AddMember(WorldUserContext user) override;
		bool								RemoveMember(uint64 userId) override;

		entt::entity						GetEntity(NetId netId);

		void								Send(Packet packet) override;
		void								HandleWorldPacket(uint64 callerUserId, Packet pkt) override;


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

		entt::entity						EnsureReplicatedActor(NetId netId, px::PrefabKey prefabKey, uint64 owner, uint64 controller, px::eBodyType bodyType);
		entt::entity						TryConfirmPendingSpawn(NetId netId, uint32 spawnReqId);
	

	private:
		bool								RegisterWorldRouting();

		void								Tick() override;
		
		void								ProcessLifecyclePacket(const PacketHeaderView& view);
		void								ProcessSnapshot(const PacketHeaderView& view);

		void								SpawnActorImpl(SpawnParams params);
		void								DespawnActorImpl(NetId netId);
		void								SetActorDormantImpl(NetId netId);
		void								PublishActorSpawned(entt::entity e, uint32 spawnReqId, bool isLocal, eActorLifecycleReason reason = eActorLifecycleReason::Spawned);
		void								PublishActorDespawned(entt::entity e, eActorLifecycleReason reason = eActorLifecycleReason::Despawned);
		void								PublishWorldParticipantEvent(uint64 participantUserId, eWorldParticipantChange change);

		void								RequestSpawnActor(const SpawnParams& params);

		void								OnSpawnActorResponse(std::optional<RPCTableRef<fb::fbSpawnActorRes>> res);
		void								OnDespawnActorResponse(std::optional<RPCTableRef<fb::fbDespawnActorRes>> res);
		void								OnPossessActorResponse(std::optional<RPCTableRef<fb::fbPossessActorRes>> res);
		void								OnUnpossesActorResponse(std::optional<RPCTableRef<fb::fbUnpossessActorRes>> res);

		void								BootstrapLevelActors();
		bool								BootstrapLocalRoute();

	private:
		ClientSessionBundle								m_sessions;


		std::string										m_levelPath;
		px::LevelLayerInfo								m_levelLayerInfo = {};
		uint16											m_localShardIndex = std::numeric_limits<uint16>::max();
		uint16											m_localWorldIndex = 0;

		uint64											m_accountId = 0;
		uint64											m_userId = 0;
		bool											m_headless = false;
		NetId											m_localNetId = NetId::Invalid();
		std::atomic<uint64>								m_latestClickMoveSeq = 0;
		std::atomic<uint32>								m_latestLocalCommandEpoch = 0;

		std::unordered_map<NetId, entt::entity>			m_netIdToEntity;		// netId -> entity (for ensure by server)
		std::unordered_map<uint32, entt::entity>		m_spawnReqIdToEntity;	// spawnReqId -> pending entities
	};


	using ClientPxWorldRef = ShardOwnedObjectRefSlot<ClientPhysicalWorld>;
}
