#include "pch.h"
#include "jamnet/runtime/ServerTransportAdapter.h"
#include "jamnet/runtime/ServerNetworkManager.h"
#include "jamnet/runtime/ServerSession.h"

namespace jam::net
{
	void ServerTransportAdapter::SetNetworkManager(ServerNetworkManager* networkManager)
	{
		WRITE_LOCK;
		m_networkManager = networkManager;
	}


	void ServerTransportAdapter::Send(const TransportInfo& info, const std::shared_ptr<SendBuffer>& buf)
	{
		ServerNetworkManager* nm = nullptr;
		{
			READ_LOCK
			nm = m_networkManager;
		}
		if (!nm)
		{
			JAMNET_LOG_WARN_LOC("ServerTransportAdapter has no ServerNetworkManager");
			return;
		}

		// base buf가 없으면 protocol 판별 불가. (FanOut에서 base buf 없이 보내고 싶으면 다음 단계에서 확장)
		if (!buf && !info.payloadFactory)
		{
			JAMNET_LOG_WARN_LOC("send buffer is nullptr");
			return;
		}


		eProtocolType protocol = eProtocolType::UDP;
		if (buf)
		{
			const PacketView view = PacketView::Parse(buf->Buffer(), buf->WriteSize());
			protocol = IsTcp(view.Channel()) ? eProtocolType::TCP : eProtocolType::UDP;
		}

		switch (info.method)
		{
		case eTransportMethod::Single:
			if (info.userId == 0) return;
			nm->SendToUser(info.userId, buf, protocol);
			return;

		case eTransportMethod::Broadcast:
			nm->EnumerateConnectedUsers([&](uint64 uid)
				{
					nm->SendToUser(uid, CloneBuffer(buf), protocol);
				});
			return;

		case eTransportMethod::Multicast:
			if (info.groupId == 0) return;
			nm->EnumerateGroupUsers(info.groupId, [&](uint64 uid)
				{
					nm->SendToUser(uid, CloneBuffer(buf), protocol);
				});
			return;

		case eTransportMethod::FanOut:
			if (info.groupId == 0) return;
			nm->EnumerateGroupUsers(info.groupId, [&](uint64 uid)
				{
					if (info.payloadFactory)
						nm->SendToUser(uid, info.payloadFactory(uid), protocol);
					else
						nm->SendToUser(uid, CloneBuffer(buf), protocol);
				});
			return;

		default: return;
		}
	}

	void ServerTransportAdapter::EnumerateGroupUsers(uint32 groupId, const std::function<void(uint64)>& fn)
	{
		if (!fn || groupId == 0)
			return;

		ServerNetworkManager* nm = nullptr;
		{
			READ_LOCK
			nm = m_networkManager;
		}
		if (!nm) return;

		nm->EnumerateGroupUsers(groupId, fn);
	}


	void ServerTransportAdapter::DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(std::weak_ptr<Session>)>& fn)
	{
		if (!fn || userId == 0)
			return;

		ServerNetworkManager* nm = nullptr;
		{
			READ_LOCK
				nm = m_networkManager;
		}
		if (!nm) return;

		if (protocol == eProtocolType::TCP)
		{
			auto tcp = nm->FindTcpSession(userId);
			fn(tcp);
			return;
		}

		if (protocol == eProtocolType::UDP)
		{
			auto udp = nm->FindUdpSession(userId);
			fn(udp);
			return;
		}

		JAMNET_LOG_ERROR_LOC("invalid protocol for RPC call");
	}

	std::shared_ptr<SendBuffer> ServerTransportAdapter::CloneBuffer(const std::shared_ptr<SendBuffer>& buf) const
	{
		if (!buf) return nullptr;

		const uint32 sz = buf->WriteSize();
		auto dup = jam::net::SendBufferManager::Instance().Open(sz);
		if (!dup) return nullptr;

		::memcpy(dup->Buffer(), buf->Buffer(), sz);
		dup->Close(sz);
		return dup;
	}
}
