#include "pch.h"
#include "jamnet/sync/networld/ClientVirtualWorld.h"

#include "jamnet/core/executor/GlobalEventBus.h"

namespace jam::net
{
	ClientVirtualWorld::ClientVirtualWorld(const WorldConfig& config)
		: VirtualWorld(config)
	{
	}

	bool ClientVirtualWorld::Init()
	{
		return VirtualWorld::Init();
	}

	bool ClientVirtualWorld::AddMember(WorldUserContext user)
	{
		const uint64 participantUserId = user.userId;
		const bool added = WorldMembershipHost::AddMember(std::move(user));
		if (added)
			PublishWorldParticipantEvent(participantUserId, eWorldParticipantChange::Joined);
		return added;
	}

	bool ClientVirtualWorld::RemoveMember(uint64 userId)
	{
		const bool removed = WorldMembershipHost::RemoveMember(userId);
		if (removed)
			PublishWorldParticipantEvent(userId, eWorldParticipantChange::Left);
		return removed;
	}

	void ClientVirtualWorld::PublishWorldParticipantEvent(uint64 participantUserId, eWorldParticipantChange change)
	{
		if (participantUserId == 0 || GetLocalWorldId() == kInvalidLocalWorldId || !GetWorldKey().IsIssued())
			return;

		WorldParticipantEvent event{};
		event.accountId = m_accountId;
		event.userId = m_userId;
		event.change = change;
		event.participant = WorldParticipantView
		{
			.key = GetWorldKey(),
			.localWorldId = GetLocalWorldId(),
			.kind = m_config.templateData.kind,
			.participantUserId = participantUserId,
		};
		GLOBAL_EVENTBUS_PUBLISH(event);
	}
}
