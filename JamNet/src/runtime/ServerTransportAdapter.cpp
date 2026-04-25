#include "pch.h"
#include "jamnet/runtime/ServerTransportAdapter.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/runtime/ServerNetworkManager.h"
#include "jamnet/runtime/ServerSession.h"

namespace jam::net
{
	void ServerTransportAdapter::SetNetworkManager(ServerNetworkManager* networkManager)
	{
		m_networkManager.store(networkManager, std::memory_order_release);
	}


	void ServerTransportAdapter::Send(const TransportInfo& info, Packet packet)
	{
		ServerNetworkManager* nm = m_networkManager.load(std::memory_order_acquire);
		if (!nm)
		{
			JAMNET_LOG_WARN_LOC("ServerTransportAdapter has no ServerNetworkManager");
			return;
		}

		// base packet이 없으면 protocol 판별 불가. (FanOut에서는 payloadFactory로 생성 가능)
		if (!packet.IsValid() && !info.payloadFactory)
		{
			JAMNET_LOG_WARN_LOC("send packet is invalid");
			return;
		}


		eProtocolType protocol = eProtocolType::UDP;
		if (packet.IsValid())
		{
			const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
			if (!view.IsValid())
				return;

			protocol = IsTcp(view.Channel()) ? eProtocolType::TCP : eProtocolType::UDP;
		}

		switch (info.method)
		{
		case eTransportMethod::Single:
			if (info.userId == 0) return;
			nm->SendToUser(info.userId, packet, protocol);
			return;

		case eTransportMethod::Broadcast:
			nm->EnumerateConnectedUsers([&](uint64 uid)
				{
					nm->SendToUser(uid, ClonePacket(packet), protocol);
				});
			return;

		case eTransportMethod::Multicast:
			if (info.worldId == 0) return;
			nm->EnumerateWorldUsers(info.worldId, [&](uint64 uid)
				{
					nm->SendToUser(uid, ClonePacket(packet), protocol);
				});
			return;

		case eTransportMethod::FanOut:
			if (info.worldId == 0) return;
			nm->EnumerateWorldUsers(info.worldId, [&](uint64 uid)
				{
					if (info.payloadFactory)
						nm->SendToUser(uid, info.payloadFactory(uid), protocol);
					else
						nm->SendToUser(uid, ClonePacket(packet), protocol);
				});
			return;

		default: return;
		}
	}

	void ServerTransportAdapter::EnumerateWorldUsers(uint32 worldId, const std::function<void(uint64)>& fn)
	{
		if (!fn || worldId == 0)
			return;

		ServerNetworkManager* nm = m_networkManager.load(std::memory_order_acquire);
		if (!nm) return;

		nm->EnumerateWorldUsers(worldId, fn);
	}


	void ServerTransportAdapter::DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(Session*)>& fn)
	{
		if (!fn || userId == 0)
			return;

		ServerNetworkManager* nm = m_networkManager.load(std::memory_order_acquire);
		if (!nm) return;

		if (protocol == eProtocolType::TCP)
		{
			if (Session* tcp = nm->FindTcpSession(userId))
				fn(tcp);
			return;
		}

		if (protocol == eProtocolType::UDP)
		{
			if (Session* udp = nm->FindUdpSession(userId))
				fn(udp);
			return;
		}

		JAMNET_LOG_ERROR_LOC("invalid protocol for RPC call");
	}

	Packet ServerTransportAdapter::ClonePacket(const Packet& packet) const
	{
		if (!packet.IsValid()) return {};

		const uint32 sz = packet->Size();
		BufWriter writer(GetNetBufferPool(eNetBufferPoolKind::Clone));
		BufferSlice slice = writer.OpenForPayload(sz, alignof(PacketHeader));
		WritePayload(slice, packet->Head(), sz);
		slice.Close();

		return MakeOwned(slice);
	}
}
