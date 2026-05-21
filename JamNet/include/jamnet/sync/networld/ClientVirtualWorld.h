#pragma once

#include "jamnet/runtime/AppRuntimeEvents.h"
#include "jamnet/runtime/world/VirtualWorld.h"

namespace jam::net
{
	class ClientVirtualWorld : public VirtualWorld
	{
	public:
		explicit ClientVirtualWorld(const WorldConfig& config);
		~ClientVirtualWorld() override = default;

		bool Init() override;
		void SetAccountId(uint64 accountId) { m_accountId = accountId; }
		void SetUserId(uint64 userId) { m_userId = userId; }
		bool AddMember(WorldUserContext user) override;
		bool RemoveMember(uint64 userId) override;

	private:
		void PublishWorldParticipantEvent(uint64 participantUserId, eWorldParticipantChange change);

	private:
		uint64 m_accountId = 0;
		uint64 m_userId = 0;
	};

	using ClientVxWorldRef = ShardOwnedObjectRefSlot<ClientVirtualWorld>;
}
