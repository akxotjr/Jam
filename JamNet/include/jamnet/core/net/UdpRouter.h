#pragma once
#include <array>
#include <atomic>
#include <limits>

#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/IocpCore.h"
#include "jamnet/core/net/NetAddress.h"
#include "jamnet/core/net/Session.h"

namespace jam::net
{
	class Service;

	enum class UdpIngressRouteKind : uint8
	{
		None			= 0,
		PrebindRoute	= 1,
		BoundSession	= 2,
	};

	struct UdpIngressRoute
	{
		UdpIngressRouteKind  kind		= UdpIngressRouteKind::None;
		RouteKey			 routeKey	= {};
		SessionId			 sessionId	= kInvalidSessionId;
	};

	class UdpRouter final : public IocpObject
	{
		static constexpr int32 OUTSTANDING_RECVS = 32;
		static constexpr size_t INGRESS_TABLE_CAPACITY = 1u << 15;

		struct RoutingSlot
		{
			std::atomic<uint64> key		= 0;
			std::atomic<uint64> value	= 0;
			std::atomic<uint8>  kind	= static_cast<uint8>(UdpIngressRouteKind::None);
		};

	public:
		UdpRouter() = default;
		~UdpRouter() override = default;

		bool                    Start(Service* service);
		void					CloseSocket();

		HANDLE					GetHandle() override;
		void					Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;


		void					RegisterSend(std::vector<PacketChain>&& chains, const NetAddress& to);
		void					RegisterRecv();

		void					ProcessSend(int32 numOfBytes, const NetAddress& remoteAddr);
		void					ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddr, Packet packet, uint64 ingressRecvTime_ns);

		void					HandleError(int32 errorCode);

		void					RegisterIngressPrebindRoute(uint64 endpointId, RouteKey ownerRouteKey);
		void					PromoteIngressToBound(uint64 endpointId, SessionId sessionId);
		void					ClearIngressRoute(uint64 endpointId);
		bool					TryGetIngressRoute(uint64 endpointId, UdpIngressRoute& out) const;

	private:
		static size_t			StartIngressIndex(uint64 endpointId);
		void					UpsertIngressRoute(uint64 endpointId, UdpIngressRouteKind kind, uint64 value);

	private:
		inline static constexpr uint64 kEmptyIngressKey = 0;
		inline static constexpr uint64 kTombstoneIngressKey = std::numeric_limits<uint64>::max();

		Service*				m_service		= nullptr;

		SOCKET					m_socket		= INVALID_SOCKET;
		SOCKADDR_IN				m_remoteSockAddr{};
		std::array<RoutingSlot, INGRESS_TABLE_CAPACITY> m_ingressRoutes = {};
	};
}
