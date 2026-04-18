#include "pch.h"
#include "jamnet/runtime/ClientTransportAdapter.h"

#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/Session.h"

namespace jam::net
{
	void ClientTransportAdapter::SetTcpSession(std::weak_ptr<Session> session)
	{
		m_tcp = std::move(session);
	}

	void ClientTransportAdapter::SetUdpSession(std::weak_ptr<Session> session)
	{
		m_udp = std::move(session);
	}

	void ClientTransportAdapter::Send(const TransportInfo& info, Packet packet)
	{
		(void)info;

		if (!packet.IsValid())
		{
			JAMNET_LOG_WARN_LOC("send buffer is nullptr");
			return;
		}

		// client는 1:1만 지원
		if (info.method != eTransportMethod::Single)
		{
			JAMNET_LOG_WARN_LOC("ClientTransportAdapter supports SINGLE only. method={}", static_cast<int>(info.method));
			return;
		}

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		if (IsTcp(view.Channel()))
		{
			if (auto tcp = m_tcp.lock()) tcp->Send(packet);
			return;
		}

		if (auto udp = m_udp.lock()) udp->Send(packet);
	}



	void ClientTransportAdapter::DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(std::weak_ptr<Session>)>& fn)
	{
		(void)userId;

		if (protocol == eProtocolType::TCP)
		{
			fn(m_tcp); return;
		}

		if (protocol == eProtocolType::UDP)
		{
			fn(m_udp); return;
		}

		JAMNET_LOG_ERROR_LOC("invalid protocol for RPC call");
	}
}
