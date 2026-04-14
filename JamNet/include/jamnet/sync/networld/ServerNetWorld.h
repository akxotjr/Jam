#pragma once
#include "jamnet/sync/networld/NetWorld.h"
#include "jamnet/sync/transport/ITransportEndpoint.h"

#include <jampx/IPhysicsFacade.h>

#include "jamnet/sync/replication/NetActorComponents.h"

namespace jam::net
{
	struct TransportInfo;
	class ITransportEndpoint;

	class ServerNetWorld : public NetWorld
	{
	public:
		ServerNetWorld() = default;
		~ServerNetWorld() override = default;

		void								Init() override;

		void								SetTransportAdapter(ITransportEndpoint* transport);
		void								SetPhysicsFacade(std::unique_ptr<px::IPhysicsFacade> physics);
		void								SetLevelPath(const std::string& levelPath) { m_levelPath = levelPath; }


		bool								Enter(uint64 userId, std::function<void()> onEntered = {});
		bool								Leave(uint64 userId, std::function<void()> onLeft = {});

		void								Send(const TransportInfo& info, const std::shared_ptr<SendBuffer>& buf);
		void								Multicast(const std::shared_ptr<SendBuffer>& buf);
		void								FanOut(TransportInfo::PayloadFactory factory);
		
		void								OnRecvPacket(uint64 callerUserId, const PacketView& pkt);

		
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

		void								GetMembers(OUT std::vector<uint64>& users) { users = m_members; };
		entt::entity						GetEntity(NetId netId) const;


	private:
		void								TickOnShard() override;
		void								BootstrapLevelActors();

		NetId								SpawnActorImpl(SpawnParams params);
		bool								DespawnActorImpl(NetId netId, uint64 userId = 0);
		bool								PossessActorImpl(NetId netId, uint64 userId = 0);
		bool								UnpossessActorImpl(NetId netId, uint64 userId = 0);

		void								ProcessGameInput(uint64 callerUserId, const PacketView& pkt);

	private:
		std::atomic<uint32>						m_netIdGenerator{ 1 };
		ITransportEndpoint*						m_transport			= nullptr;
		std::unique_ptr<px::IPhysicsFacade>		m_physics			= nullptr;

		std::string								m_levelPath;
		px::LevelLayerInfo						m_levelLayerInfo	= {};

		std::atomic<bool>						m_pendingInitialFullSnapshot{ false };

		std::vector<uint64>						m_members;
		std::unordered_map<NetId, entt::entity>	m_netIdToEntity;
	};
}
