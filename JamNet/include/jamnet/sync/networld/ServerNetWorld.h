#pragma once
#include "NetWorld.h"
#include "jamnet/sync/transport/ITransportEndpoint.h"

#include <jampx/IPhysicsFacade.h>

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
		void								SetPhysicsFacade(unique_ptr<px::IPhysicsFacade> physics);

		void								Enter(uint64 userId);
		void								Leave(uint64 userId);

		void								Send(const TransportInfo& info, const shared_ptr<SendBuffer>& buf);
		void								Multicast(const shared_ptr<SendBuffer>& buf);
		void								FanOut(TransportInfo::PayloadFactory factory);
		
		void								OnRecvPacket(const PacketView& pkt);

		
		void								SpawnActor(const SpawnParams& params);
		void								DespawnActor(uint32 netId, uint64 userId);
		void								PossessActor(uint32 netId, uint64 userId);
		void								UnpossessActor(uint32 netId, uint64 userId);


		void								SpawnActorAsync(const SpawnParams& params, std::function<void(uint32)> onDone);
		void								DespawnActorAsync(uint32 netId, uint64 userId, std::function<void(bool)> onDone);
		void								PossessActorAsync(uint32 netId, uint64 userId, std::function<void(bool)> onDone);
		void								UnpossessActorAsync(uint32 netId, uint64 userId, std::function<void(bool)> onDone);

		void								RequestInitialSnapshotNewClient();

		void								GetMembers(OUT vector<uint64>& users) { users = m_members; };

		void								SetLevelPath(const string& levelPath) { m_levelPath = levelPath; }

	private:
		void								TickOnShard() override;

		uint32								SpawnActorImpl(const SpawnParams& params);
		bool								DespawnActorImpl(uint32 netId, uint64 userId = 0);
		bool								PossessActorImpl(uint32 netId, uint64 userId = 0);
		bool								UnpossessActorImpl(uint32 netId, uint64 userId = 0);

		void								ProcessGameInput(const PacketView& pkt);

	private:
		atomic<uint32>						m_netIdGenerator{ 1 };
		ITransportEndpoint*					m_transport;
		unique_ptr<px::IPhysicsFacade>		m_physics = nullptr;

		string								m_levelPath;

		atomic<bool>						m_pendingInitialFullSnapshot{ false };

		vector<uint64>						m_members;
	};
}
