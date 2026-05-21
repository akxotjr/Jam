#include "pch.h"
#include "jamnet/sync/networld/ServerVirtualWorld.h"

#include "jamnet/core/net/PacketBuilder.h"

#include "jamnet/runtime/ServerSession.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"

namespace jam::net
{
	namespace
	{
		Packet ClonePacket(const Packet& packet)
		{
			if (!packet.IsValid())
				return {};

			const uint32 size = packet->Size();
			BufWriter writer(GetNetBufferPool(eNetBufferPoolKind::Clone));
			BufferSlice slice = writer.OpenForPayload(size, alignof(PacketHeader));
			WritePayload(slice, packet->Head(), size);
			slice.Close();
			return MakeOwned(slice);
		}

		eProtocolType ResolveProtocol(const Packet& packet)
		{
			if (!packet.IsValid())
				return eProtocolType::NONE;

			const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
			if (!view.IsValid())
				return eProtocolType::NONE;

			return IsTcp(view.Channel()) ? eProtocolType::TCP : eProtocolType::UDP;
		}

		Session* SelectSession(const ServerSessionBundle& sessions, eProtocolType protocol)
		{
			if (protocol == eProtocolType::TCP)
				return sessions.TryGetTcp();

			if (protocol == eProtocolType::UDP)
				return sessions.TryGetUdp();

			return nullptr;
		}

		void SendToSession(Session* session, Packet packet)
		{
			if (!session || !packet.IsValid() || !session->IsConnected())
				return;

			session->Send(packet);
		}
	}

	ServerVirtualWorld::ServerVirtualWorld(const WorldConfig& config)
		: VirtualWorld(config)
	{
	}

	bool ServerVirtualWorld::Init()
	{
		return VirtualWorld::Init();
	}

	bool ServerVirtualWorld::AddMember(WorldUserContext user)
	{
		return WorldMembershipHost::AddMember(std::move(user));
	}

	bool ServerVirtualWorld::RemoveMember(uint64 userId)
	{
		return WorldMembershipHost::RemoveMember(userId);
	}

	void ServerVirtualWorld::SendTo(Packet packet, uint64 userId)
	{
		if (!packet.IsValid())
		{
			JAMNET_LOG_WARN_LOC("send packet is invalid");
			return;
		}

		const eProtocolType protocol = ResolveProtocol(packet);
		if (protocol == eProtocolType::NONE)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		if (auto it = m_userContexts.find(userId); it != m_userContexts.end())
			SendToSession(SelectSession(it->second.sessions, protocol), std::move(packet));
	}

	void ServerVirtualWorld::Multicast(Packet packet)
	{
		if (!packet.IsValid())
			return;

		const eProtocolType protocol = ResolveProtocol(packet);
		if (protocol == eProtocolType::NONE)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		for (const auto& p : m_userContexts | std::views::values)
			SendToSession(SelectSession(p.sessions, protocol), ClonePacket(packet));
	}

	void ServerVirtualWorld::UpdateMemberContext(WorldUserContext user)
	{
		WorldMembershipHost::UpdateMemberContext(std::move(user));
	}

	void ServerVirtualWorld::RemoveMemberContext(uint64 userId)
	{
		WorldMembershipHost::RemoveMemberContext(userId);
	}
}
