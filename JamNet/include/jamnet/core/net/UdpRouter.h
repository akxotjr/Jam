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
	struct UdpRecvEvent;

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
		uint32				 generation	= 0;
	};

	class UdpRouter final : public IocpObject
	{
		static constexpr int32 OUTSTANDING_RECVS = 32;
		static constexpr size_t INGRESS_TABLE_CAPACITY = 1u << 15;

		struct RoutingSlot
		{
			std::atomic<uint64> key			= 0;
			std::atomic<uint32> sequence	= 0;
			std::atomic<uint64> value		= 0;
			std::atomic<uint32> generation	= 0;
			std::atomic<uint8>  kind		= static_cast<uint8>(UdpIngressRouteKind::None);
		};

		struct IngressBudget
		{
			std::atomic<uint32> pending = 0;
		};

		struct IngressLease
		{
			std::shared_ptr<IngressBudget> budget;

			IngressLease() = default;
			explicit IngressLease(std::shared_ptr<IngressBudget> value) : budget(std::move(value)) {}
			IngressLease(IngressLease&&) noexcept = default;
			IngressLease& operator=(IngressLease&& rhs) noexcept
			{
				if (this == &rhs)
					return *this;
				Release();
				budget = std::move(rhs.budget);
				return *this;
			}
			IngressLease(const IngressLease&) = delete;
			IngressLease& operator=(const IngressLease&) = delete;
			~IngressLease() { Release(); }

		private:
			void Release()
			{
				if (budget)
				{
					budget->pending.fetch_sub(1, std::memory_order_release);
					budget.reset();
				}
			}
		};

		struct BoundIngressState;

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
		void					ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddr, UdpRecvEvent* recvEvent, uint64 ingressRecvTime_ns);

		void					HandleError(int32 errorCode);

		void					RegisterIngressPrebindRoute(uint64 endpointId, RouteKey ownerRouteKey, uint32 generation = 0);
		void					PromoteIngressToBound(uint64 endpointId, SessionId sessionId);
		void					ClearIngressRoute(uint64 endpointId);
		bool					ClearIngressPrebindRoute(uint64 endpointId, RouteKey ownerRouteKey, uint32 generation = 0);
		bool					ClearIngressBoundRoute(uint64 endpointId, SessionId sessionId);
		bool					TryGetIngressRoute(uint64 endpointId, UdpIngressRoute& out) const;

	private:
		static size_t					StartIngressIndex(uint64 endpointId);
		void							UpsertIngressRoute(uint64 endpointId, UdpIngressRouteKind kind, uint64 value, uint32 generation = 0);
		bool							ClearIngressRouteIfMatches(uint64 endpointId, UdpIngressRouteKind kind, uint64 value, uint32 generation);
		std::shared_ptr<IngressBudget> TryAcquireIngressBudget(uint32 shardIndex);
		static void						ScheduleBoundIngressDrain(uint16 shardIndex, const std::shared_ptr<BoundIngressState>& ingress);
		static void						DrainBoundIngress(uint16 shardIndex, const std::shared_ptr<BoundIngressState>& ingress);

	private:
		static constexpr uint64 kEmptyIngressKey = 0;
		static constexpr uint64 kTombstoneIngressKey = std::numeric_limits<uint64>::max();

		Service*				m_service		= nullptr;

		SOCKET					m_socket		= INVALID_SOCKET;
		SOCKADDR_IN				m_remoteSockAddr{};

		std::array<RoutingSlot, INGRESS_TABLE_CAPACITY> m_ingressRoutes = {};
		std::vector<std::shared_ptr<IngressBudget>>		m_ingressBudgets;
		std::vector<std::shared_ptr<BoundIngressState>> m_boundIngress;
	};
}
