#pragma once

#include "jamnet/runtime/session/ClientPrincipalState.h"

#include "jamnet/runtime/world/simulation/common/PhysicalWorld.h"
#include "jamnet/runtime/world/simulation/common/CharacterControlTypes.h"

#include "jamnet/runtime/application/AppRuntimeEvents.h"
#include "jamnet/runtime/protocol/schema/RPCSchemaIds.h"

#include "jamnet/runtime/protocol/schema/gen/actor_spawn_generated.h"


#include <functional>




namespace jam::net
{
	class ClientReplicationSystem;
	class ClientCharacterControlCoordinator;

	class ClientWorld : public PhysicalWorld
	{
		friend class ClientReplicationSystem;
		friend class ClientCharacterControlCoordinator;
		friend class ClientPhysicsSystem;

	public:
		ClientWorld() = default;
		explicit ClientWorld(const WorldConfig& config) : PhysicalWorld(config) {}
		~ClientWorld() override = default;

		void								Start(uint64 dt_ns) override;
		void								Resume(uint64 dt_ns) override;
		void								Stop() override;
		void								SetHeadless(bool headless) { m_headless = headless; }
		bool								IsHeadless() const { return m_headless; }
		void								SetPipelineSubtype(uint16 subtype) { m_pipelineSubtype = subtype; }

		void								SetPrincipalState(ClientPrincipalState* principal) { m_principal = principal; }
		uint64								GetAccountId() const { return m_principal ? m_principal->accountId : kInvalidAccountId; }
		uint64								GetUserId()	   const { return m_principal ? m_principal->userId : kInvalidUserId; }
		
		bool								AddMember(WorldUserContext user) override;
		bool								RemoveMember(uint64 userId) override;

		void								Send(Packet packet) override;
		void								HandleWorldPacket(uint64 callerUserId, Packet pkt) override;

		void								SubmitActorAction(const ActorActionCommand& command, const WorldEventCorrelation& correlation = {});
		void								SubmitCharacterControl(CharacterControlIntent intent);
		
		void								RequestHitscan(const px::Vec3& from, const px::Vec3& dir, float maxRange, std::function<void(const px::HitscanResult&)> onDone);

		void								SetReplicatedActorDormant(ActorId actorId);
		void								HideReplicatedActorUntilConfirmed(ActorId actorId);
		void								ReactivateReplicatedActor(ActorId actorId, bool isLocal);
		void								DestroyReplicatedActor(ActorId actorId);

		entt::entity						EnsureReplicatedActor(ActorId actorId, ActorArchetypeKey actorArchetypeKey, uint64 owner, uint64 controller, px::eBodyType bodyType, bool* outCreated = nullptr);
	

	private:
		bool								OnInitialize() override;
		void								OnShutdown() override;

		void								Tick() override;
		
		void								ProcessLifecyclePacket(const PacketHeaderView& view);
		void								ProcessSnapshot(const PacketHeaderView& view);

		bool								BuildSpawnParams(const FrontendSpawnActorSpec& spec, ClientRequestId requestId, OUT SpawnParams& outParams) const;
		void								DespawnActorImpl(ActorId actorId);
		void								RollbackPhysicsSpawn(entt::entity entity);
		void								SetActorDormantImpl(ActorId actorId);
		void								PublishActorSpawned(entt::entity e, ClientRequestId requestId, bool isLocal, eActorLifecycleReason reason = eActorLifecycleReason::Spawned);
		void								PublishActorDespawned(entt::entity e, eActorLifecycleReason reason = eActorLifecycleReason::Despawned);
		void								PublishWorldParticipantEvent(uint64 participantUserId, eWorldParticipantChange change);

		void								RequestSpawnActor(const SpawnParams& params);
		void								RequestSpawnPlayer(const SpawnParams& params);
		void								RequestDespawnActor(const DespawnActorRequest& request);
		void								RequestDespawnPlayer(const DespawnActorRequest& request);
		void								PublishActorActionResult(ClientRequestId requestId, ActorActionResult result) const;
		bool								TryResolvePhysicsArchetypeKey(ActorArchetypeKey actorArchetypeKey, OUT px::PhysicsArchetypeKey& outKey) const;

		void								OnSpawnActorResponse(ClientRequestId requestId, std::optional<RPCTableRef<fb::fbSpawnActorRes>> res);
		void								OnSpawnPlayerResponse(ClientRequestId requestId, std::optional<RPCTableRef<fb::fbSpawnPlayerRes>> res);
		void								OnDespawnActorResponse(ClientRequestId requestId, ActorId actorId, std::optional<RPCTableRef<fb::fbDespawnActorRes>> res);
		void								OnDespawnPlayerResponse(ClientRequestId requestId, ActorId actorId, std::optional<RPCTableRef<fb::fbDespawnPlayerRes>> res);

		void								BootstrapLevelActors();

	private:
		ClientPrincipalState*				m_principal		  = nullptr;

		uint16								m_pipelineSubtype = 0;
		bool								m_headless = false;
		ActorId								m_localActorId = ActorId::Invalid();

	};
}
