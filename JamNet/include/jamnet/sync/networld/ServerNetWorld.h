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


		void								Enter(uint64 userId);
		void								Leave(uint64 userId);

		void								Send(const TransportInfo& info, const std::shared_ptr<SendBuffer>& buf);
		void								Multicast(const std::shared_ptr<SendBuffer>& buf);
		void								FanOut(TransportInfo::PayloadFactory factory);
		
		void								OnRecvPacket(const PacketView& pkt);

		
		void								SpawnActor(SpawnParams params);
		void								DespawnActor(NetId netId, uint64 userId);
		void								PossessActor(NetId netId, uint64 userId);
		void								UnpossessActor(NetId netId, uint64 userId);


		void								SpawnActorAsync(SpawnParams params, std::function<void(NetId)> onDone);
		void								DespawnActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone);
		void								PossessActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone);
		void								UnpossessActorAsync(NetId netId, uint64 userId, std::function<void(bool)> onDone);

		void								RequestInitialSnapshotNewClient();

		void								GetMembers(OUT std::vector<uint64>& users) { users = m_members; };


	private:
		void								TickOnShard() override;
		void								BootstrapLevelActors();

		NetId								SpawnActorImpl(SpawnParams params);
		bool								DespawnActorImpl(NetId netId, uint64 userId = 0);
		bool								PossessActorImpl(NetId netId, uint64 userId = 0);
		bool								UnpossessActorImpl(NetId netId, uint64 userId = 0);

		void								ProcessGameInput(const PacketView& pkt);

	private:
		std::atomic<uint32>						m_netIdGenerator{ 1 };
		ITransportEndpoint*						m_transport;
		std::unique_ptr<px::IPhysicsFacade>		m_physics = nullptr;

		std::string								m_levelPath;
		px::LevelLayerInfo						m_levelLayerInfo = {};

		std::atomic<bool>						m_pendingInitialFullSnapshot{ false };

		std::vector<uint64>						m_members;
	};
}
