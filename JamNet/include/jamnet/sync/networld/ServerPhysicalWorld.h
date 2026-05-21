#pragma once
#include "jamnet/runtime/world/PhysicalWorld.h"

#include <jampx/IPhysicsFacade.h>

#include "jamnet/sync/replication/NetActorComponents.h"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

namespace jam::net
{
	struct PacketHeaderView;

	class ServerPhysicalWorld : public PhysicalWorld
	{
	public:
		ServerPhysicalWorld(const WorldConfig& config);
		~ServerPhysicalWorld() override = default;

		bool								Init() override;
		void								Start(uint64 dt_ns) override;
		void								Resume(uint64 dt_ns) override;
		void								Stop() override;

		void								SetPhysicsFacade(std::unique_ptr<px::IPhysicsFacade> physics);
		void								SetLevelPath(const std::string& levelPath) { m_levelPath = levelPath; }
		void								HandleWorldPacket(uint64 callerUserId, Packet packet) override;

		bool								AddMember(WorldUserContext user) override;
		bool								RemoveMember(uint64 userId) override;

		void								SendTo(Packet packet, uint64 userId) override;
		void								Multicast(Packet packet) override;

		void								UpdateMemberContext(WorldUserContext user) override;
		void								RemoveMemberContext(uint64 userId) override;
		Session*							GetMemberSession(uint64 userId, eProtocolType protocol) override { return WorldMembershipHost::GetMemberSession(userId, protocol); }

		void								SpawnActor(SpawnParams params);
		void								DespawnActor(NetId netId, uint64 userId);
		bool								DespawnActorImmediate(NetId netId, uint64 userId = 0);
		void								PossessActor(NetId netId, uint64 userId);
		void								UnpossessActor(NetId netId, uint64 userId);


		void								SpawnActorAsync(SpawnParams params, std::function<void(NetId)> onDone);
		void								DespawnActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone);
		void								PossessActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone);
		void								UnpossessActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone);

		void								RequestInitialSnapshotNewClient();

		entt::entity						GetEntity(NetId netId) const;
		entt::entity						GetControlledEntity(uint64 userId) const;


	private:
		void								Tick() override;
		void								OnUserJoined(uint64 userId) override;
		void								OnUserLeft(uint64 userId) override;
		void								BootstrapLevelActors();

		NetId								SpawnActorImpl(SpawnParams params);
		bool								DespawnActorImpl(NetId netId, uint64 userId = 0);
		bool								PossessActorImpl(NetId netId, uint64 userId = 0);
		bool								UnpossessActorImpl(NetId netId, uint64 userId = 0);

		void								ProcessGameInput(uint64 callerUserId, const PacketHeaderView& pkt);

	private:
		std::atomic<uint32>							m_netIdGenerator{ 1 };

		std::string									m_levelPath;
		px::LevelLayerInfo							m_levelLayerInfo	= {};

		std::atomic<bool>							m_pendingInitialFullSnapshot{ false };

		std::unordered_map<NetId, uint64>			m_ownerByNetId;
		std::unordered_map<NetId, uint64>			m_controllerByNetId;
		std::unordered_map<NetId, entt::entity>		m_entityByNetId;
		std::unordered_map<entt::entity, NetId>		m_netIdByEntity;


		std::unordered_map<NetId, entt::entity>		m_netIdToEntity;
		std::unordered_map<uint64, entt::entity>	m_userToControlledEntity;
	};
}
