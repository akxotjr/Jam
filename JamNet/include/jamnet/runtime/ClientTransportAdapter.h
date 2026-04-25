#pragma once

#include "jamnet/core/net/Session.h"
#include "jamnet/sync/transport/ITransportEndpoint.h"

namespace jam::net
{
	/// @brief Client 용 Transport Adapter (ITransportSystem 구현)
	/// @details ClientNetWorld 와 Client Session 간의 송수신을 중개.
	class ClientTransportAdapter : public ITransportEndpoint
	{
	public:
		ClientTransportAdapter() = default;
		~ClientTransportAdapter() override = default;

		void SetTcpSession(Session* session);
		void SetUdpSession(Session* session);

		void Send(const TransportInfo& info, Packet packet) override;

		void EnumerateWorldUsers(uint32 worldId, const std::function<void(uint64)>& fn) override {};

	protected:
		void DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(Session*)>& fn) override;

	private:
		struct CachedSessionRef
		{
			Session*		ptr		= nullptr;
			SessionHandle	handle	= {};

			void Set(Session* session)
			{
				ptr    = session;
				handle = session ? session->GetSessionHandle() : SessionHandle{};
			}

			Session* TryGetRaw() const
			{
				if (ptr && ptr->MatchesSessionHandle(handle))
					return ptr;
				return nullptr;
			}
		};

	private:
		CachedSessionRef m_tcp;
		CachedSessionRef m_udp;
	};
}
