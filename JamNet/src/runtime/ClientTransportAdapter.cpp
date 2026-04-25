#include "pch.h"
#include "jamnet/runtime/ClientTransportAdapter.h"

#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/Session.h"

namespace jam::net
{
	void ClientTransportAdapter::SetTcpSession(Session* session)
	{
		m_tcp.Set(session);
	}

	void ClientTransportAdapter::SetUdpSession(Session* session)
	{
		m_udp.Set(session);
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
			if (Session* tcp = m_tcp.TryGetRaw())
				tcp->Send(packet);
			return;
		}

		if (Session* udp = m_udp.TryGetRaw())
			udp->Send(packet);
	}



	void ClientTransportAdapter::DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(Session*)>& fn)
	{
		(void)userId;

		if (protocol == eProtocolType::TCP)
		{
			fn(m_tcp.TryGetRaw()); return;
		}

		if (protocol == eProtocolType::UDP)
		{
			fn(m_udp.TryGetRaw()); return;
		}

		JAMNET_LOG_ERROR_LOC("invalid protocol for RPC call");
	}
}
