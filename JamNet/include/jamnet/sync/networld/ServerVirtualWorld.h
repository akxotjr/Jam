#pragma once

#include "jamnet/runtime/world/VirtualWorld.h"
#include <unordered_map>
#include <vector>

namespace jam::net
{
	class ServerVirtualWorld : public VirtualWorld
	{
	public:
		ServerVirtualWorld(const WorldConfig& config);
		~ServerVirtualWorld() override = default;

		bool		Init() override;

		bool		AddMember(WorldUserContext user) override;
		bool		RemoveMember(uint64 userId) override;

		void		SendTo(Packet packet, uint64 userId) override;
		void		Multicast(Packet packet) override;

		void		UpdateMemberContext(WorldUserContext user) override;
		void		RemoveMemberContext(uint64 userId) override;
		Session*	GetMemberSession(uint64 userId, eProtocolType protocol) override { return WorldMembershipHost::GetMemberSession(userId, protocol); }
	};
}
