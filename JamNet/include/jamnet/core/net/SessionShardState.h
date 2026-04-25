#pragma once

#include "jamnet/core/net/NetAddress.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace jam::net
{
	class TcpSession;
	class UdpSession;

	struct SessionTableKey
	{
		SOCKET		localSocket = INVALID_SOCKET;
		NetAddress	remoteAddr = {};

		bool operator==(const SessionTableKey& other) const
		{
			return localSocket == other.localSocket && remoteAddr == other.remoteAddr;
		}
	};

	struct SessionTableKeyHash
	{
		size_t operator()(const SessionTableKey& key) const noexcept
		{
			const size_t socketHash =
				std::hash<uintptr_t>()(static_cast<uintptr_t>(key.localSocket));

			const size_t addrHash =
				std::hash<NetAddress>()(key.remoteAddr);

			return socketHash ^ (addrHash << 1);
		}
	};

	using TcpSessionTable		 = std::unordered_map<SessionTableKey, std::unique_ptr<TcpSession>, SessionTableKeyHash>;
	using UdpSessionTable		 = std::unordered_map<SessionTableKey, std::unique_ptr<UdpSession>, SessionTableKeyHash>;
	using DetachedTcpSessionList = std::vector<std::unique_ptr<TcpSession>>;
	using DetachedUdpSessionList = std::vector<std::unique_ptr<UdpSession>>;

	struct SessionShardState
	{
		TcpSessionTable			tcpSessions;
		UdpSessionTable			udpSessions;
		DetachedTcpSessionList	detachedTcpSessions;
		DetachedUdpSessionList	detachedUdpSessions;

		SessionShardState();
		~SessionShardState();
	};
}
