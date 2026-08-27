#include "pch.h"
#include "jamnet/core/net/UdpRouter.h"

#include "jambase/EnumUtils.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/memory/ObjectPool.h"
#include "jamnet/core/net/AdmissionContext.h"
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/PacketWireTime.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/UdpSession.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/net/WinErrorHandling.h"

#include <concurrentqueue/moodycamel/concurrentqueue.h>

#pragma warning(disable : 4996)

namespace jam::net
{
	namespace
	{
		constexpr int32 kDefaultClientUdpSocketBufferSize = 256 * 1024;
		constexpr int32 kDefaultServerUdpSocketBufferSize = 16 * 1024 * 1024;

		int32 ResolveUdpSocketBufferSize(eServiceType serviceType, int32 configuredSize)
		{
			if (configuredSize > 0)
				return configuredSize;

			return serviceType == eServiceType::SERVER
				? kDefaultServerUdpSocketBufferSize
				: kDefaultClientUdpSocketBufferSize;
		}

		void RecycleUdpRecvEvent(UdpRecvEvent* event)
		{
			if (!event)
				return;
			event->reservation.Reset();
			ObjectPool<UdpRecvEvent>::Push(event);
		}

		struct UdpRecvEventDeleter
		{
			void operator()(UdpRecvEvent* event) const
			{
				RecycleUdpRecvEvent(event);
			}
		};

		using OwnedUdpRecvEvent = std::unique_ptr<UdpRecvEvent, UdpRecvEventDeleter>;

		Packet TakeReceivedDatagram(UdpRecvEvent& event, const uint32 numOfBytes)
		{
			return event.reservation.Finalize(numOfBytes);
		}
	}

	struct UdpRouter::BoundIngressState
	{
		static constexpr size_t kDrainBatchPacketCount = 512;
		static constexpr size_t kDrainJobPacketBudget = 2048;
		static constexpr uint64 kDrainJobTimeBudgetNs = 1_ms;

		struct Item
		{
			int32				numOfBytes = 0;
			uint64				ingressRecvTimeNs = 0;
			EndpointId			endpointId = kInvalidEndpointId;
			SessionId			sessionId = kInvalidSessionId;
			OwnedUdpRecvEvent	received;
			IngressLease		lease;
		};

		bool Enqueue(Item item)
		{
			if (!queue.enqueue(std::move(item)))
				return false;
			return pending.fetch_add(1, std::memory_order_acq_rel) == 0;
		}

		size_t TakeBatch(std::vector<Item>& out, const size_t limit)
		{
			const size_t count = std::min(limit, pending.load(std::memory_order_acquire));
			out.reserve(count);
			for (size_t i = 0; i < count; ++i)
			{
				Item item;
				if (!queue.try_dequeue(item))
					break;
				out.emplace_back(std::move(item));
			}
			return out.size();
		}

		size_t Complete(const size_t count)
		{
			const size_t previous = pending.fetch_sub(count, std::memory_order_acq_rel);
			JAM_ASSERT(previous >= count);
			return previous - count;
		}

		moodycamel::ConcurrentQueue<Item>	queue;
		std::atomic<size_t>				pending = 0;
	};

	size_t UdpRouter::StartIngressIndex(EndpointId endpointId)
	{
		return static_cast<size_t>((endpointId * 11400714819323198485ull) & (INGRESS_TABLE_CAPACITY - 1));
	}

	void UdpRouter::UpsertIngressRoute(EndpointId endpointId, UdpIngressRouteKind kind, uint64 value)
	{
		if (endpointId == kEmptyIngressKey || endpointId == kTombstoneIngressKey)
			return;

		size_t idx = StartIngressIndex(endpointId);
		for (size_t probe = 0; probe < INGRESS_TABLE_CAPACITY; ++probe, idx = (idx + 1) & (INGRESS_TABLE_CAPACITY - 1))
		{
			auto& slot = m_ingressRoutes[idx];
			uint64 key = slot.key.load(std::memory_order_acquire);
			if (key == endpointId || key == kEmptyIngressKey || key == kTombstoneIngressKey)
			{
				uint32 sequence = slot.sequence.load(std::memory_order_acquire);
				if ((sequence & 1u) != 0 || !slot.sequence.compare_exchange_strong(sequence, sequence + 1, std::memory_order_acq_rel))
				{
					--probe;
					continue;
				}

				key = slot.key.load(std::memory_order_acquire);
				if (key != endpointId && key != kEmptyIngressKey && key != kTombstoneIngressKey)
				{
					slot.sequence.store(sequence + 2, std::memory_order_release);
					--probe;
					continue;
				}

				slot.key.store(endpointId, std::memory_order_relaxed);
				slot.value.store(value, std::memory_order_relaxed);
				slot.kind.store(static_cast<uint8>(kind), std::memory_order_relaxed);
				slot.sequence.store(sequence + 2, std::memory_order_release);
				return;
			}
		}
	}

	bool UdpRouter::Start(Service* service)
	{
		m_service = service;
		if (!m_service) return false;

		uint32 shardCount = GLOBAL_EXEC.GetShardCount();

		m_ingressBudgets.clear();
		m_ingressBudgets.reserve(shardCount);
		m_boundIngress.clear();
		if (m_service->GetServiceType() == eServiceType::SERVER)
			m_boundIngress.reserve(shardCount);

		for (uint32 i = 0; i < shardCount; ++i)
		{
			m_ingressBudgets.push_back(std::make_shared<IngressBudget>());
			if (m_service->GetServiceType() == eServiceType::SERVER)
				m_boundIngress.push_back(std::make_shared<BoundIngressState>());
		}

		m_socket = SocketUtils::CreateSocket(eProtocolType::UDP);
		if (m_socket == INVALID_SOCKET)
			return false;

		if (m_service->RegisterIocpObject(this) == false)
			return false;

		if (SocketUtils::SetReuseAddress(m_socket, true) == false)
			return false;

		const eServiceType serviceType = m_service->GetServiceType();
		const int32 recvBufferSize = ResolveUdpSocketBufferSize(serviceType, m_service->GetUdpRecvBufferSize());
		const int32 sendBufferSize = ResolveUdpSocketBufferSize(serviceType, m_service->GetUdpSendBufferSize());

		if (!SocketUtils::SetRecvBufferSize(m_socket, recvBufferSize) || !SocketUtils::SetSendBufferSize(m_socket, sendBufferSize))
		{
			JAM_LOG_ERROR("[UdpRouter] Failed to configure socket buffers. serviceType={}, recvBytes={}, sendBytes={}",
				E2U(serviceType), recvBufferSize, sendBufferSize);
			return false;
		}

		if (serviceType == eServiceType::CLIENT)
		{
			if (SocketUtils::BindAnyAddress(m_socket, 0) == false)
				return false;
		}
		else if (m_service->GetServiceType() == eServiceType::SERVER)
		{
			if (SocketUtils::Bind(m_socket, m_service->GetLocalUdpNetAddress()) == false)
				return false;
		}

		for (int8 i = 0; i < OUTSTANDING_RECVS; ++i)
		{
			RegisterRecv();
		}

		return true;
	}

	void UdpRouter::CloseSocket()
	{
		MarkClosing();
		SocketUtils::Close(m_socket);
	}


	HANDLE UdpRouter::GetHandle()
	{
		return reinterpret_cast<HANDLE>(m_socket);
	}

	void UdpRouter::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
	{
		switch (iocpEvent->m_eventType)
		{
		case eEventType::UdpRecv:
		{
			auto* ev = static_cast<UdpRecvEvent*>(iocpEvent);
			const bool validReceiveStorage = m_service && ev->reservation.IsValid();
			if (validReceiveStorage && numOfBytes > 0 && numOfBytes <= static_cast<int32>(ev->reservation.Capacity()))
			{
				const uint64 ingress = CaptureWireTimestampNow();
				ProcessRecv(numOfBytes, ev->remoteAddr, ev, ingress);
			}
			else
			{
				RecycleUdpRecvEvent(ev);
			}
			RegisterRecv();
			break;
		}
		case eEventType::UdpSend:
		{
			auto* ev = static_cast<UdpSendEvent*>(iocpEvent);

			if (numOfBytes > 0)
				ProcessSend(numOfBytes, ev->remoteAddr);
			ev->chain.Clear();
			ev->wsaBufs.clear();
			ObjectPool<UdpSendEvent>::Push(ev);
			break;
		}
		default:
			break;
		}
	}


	void UdpRouter::RegisterSend(std::vector<PacketChain>&& chains, const NetAddress& to)
	{
		if (IsClosing())
			return;

		for (auto& chain : chains)
		{
			if (chain.Empty()) continue;

			auto* ev = ObjectPool<UdpSendEvent>::Pop();
			ev->Init();
			ev->remoteAddr = to;
			ev->chain      = std::move(chain);

			const auto& parts = ev->chain.Parts();
			ev->wsaBufs.clear();
			ev->wsaBufs.reserve(parts.size());

			for (const BufferSlice& part : parts)
			{
				if (!part.IsValid() || part.Size() == 0)
					continue;

				WSABUF w{};
				w.buf = reinterpret_cast<char*>(part.Head());
				w.len = static_cast<ULONG>(part.Size());
				ev->wsaBufs.push_back(w);
			}

			if (ev->wsaBufs.empty())
			{
				ev->chain.Clear();
				ev->wsaBufs.clear();
				ObjectPool<UdpSendEvent>::Push(ev);
				continue;
			}

			PatchOutgoingSystemWireTime(ev->chain, CaptureWireTimestampNow());

			if (!TryAddPendingDispatch())
			{
				ev->chain.Clear();
				ev->wsaBufs.clear();
				ObjectPool<UdpSendEvent>::Push(ev);
				continue;
			}
			if (SOCKET_ERROR == ::WSASendTo(m_socket, ev->wsaBufs.data(), static_cast<DWORD>(ev->wsaBufs.size()), nullptr, 0, ev->remoteAddr.GetSockAddrPtr(), sizeof(SOCKADDR_IN), ev, nullptr))
			{
				const int32 ec = win_error::GetLastWsaError();
				if (!win_error::IsIoPending(ec))
				{
					ReleasePendingDispatch();
					HandleError(ec);
					ev->chain.Clear();
					ev->wsaBufs.clear();
					ObjectPool<UdpSendEvent>::Push(ev);
				}
			}
		}
	}

	void UdpRouter::RegisterRecv()
	{
		if (IsClosing())
			return;

		auto* ev  = ObjectPool<UdpRecvEvent>::Pop();
		ev->Init();
		const eNetBufferPoolKind poolKind = m_service->GetServiceType() == eServiceType::SERVER
			? eNetBufferPoolKind::UdpServerIo
			: eNetBufferPoolKind::UdpClientIo;

		if (!ev->reservation.Open(GetNetBufferPool(poolKind), JAMNET_MTU, alignof(PacketHeader)))
		{
			RecycleUdpRecvEvent(ev);
			return;
		}

		ev->wsaBuf.buf		= reinterpret_cast<char*>(ev->reservation.Data());
		ev->wsaBuf.len		= static_cast<ULONG>(ev->reservation.Capacity());
		ev->remoteAddrLen   = sizeof(SOCKADDR_IN);

		if (!TryAddPendingDispatch())
		{
			RecycleUdpRecvEvent(ev);
			return;
		}


		if (SOCKET_ERROR == ::WSARecvFrom(m_socket, &ev->wsaBuf, 1, nullptr, &ev->flags, ev->remoteAddr.GetSockAddrPtr(), OUT &ev->remoteAddrLen, ev, nullptr))
		{
			const int32 error = win_error::GetLastWsaError();
			if (!win_error::IsIoPending(error))
			{
				ReleasePendingDispatch();
				HandleError(error);
				RecycleUdpRecvEvent(ev);
			}
		}
	}

	void UdpRouter::ProcessSend(int32 numOfBytes, const NetAddress& remoteAddr)
	{
		if (numOfBytes == 0)
		{
			JAM_LOG_WARN_LOC("[ip= {}, port= {}] num of bytes is 0", remoteAddr.GetIpAddress(), remoteAddr.GetPort());
		}
	}

	std::shared_ptr<UdpRouter::IngressBudget> UdpRouter::TryAcquireIngressBudget(const uint32 shardIndex)
	{
		if (!m_service || m_service->GetServiceType() != eServiceType::SERVER)
			return {};
		if (shardIndex >= m_ingressBudgets.size())
			return {};

		const auto& budget = m_ingressBudgets[shardIndex];
		const uint32 configuredLimit = m_service->GetUdpIngressPendingPerShardLimit();
		const uint32 limit = configuredLimit == 0 ? UINT32_MAX : configuredLimit;

		uint32 pending = budget->pending.load(std::memory_order_relaxed);
		while (pending < limit)
		{
			if (budget->pending.compare_exchange_weak(pending, pending + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
			{
				return budget;
			}
		}

		return {};
	}

	void UdpRouter::ScheduleBoundIngressDrain(const uint16 shardIndex, const std::shared_ptr<BoundIngressState>& ingress)
	{
		const auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
		if (!shard)
			return;

		shard->Submit(Job([shardIndex, ingress]()
			{
				DrainBoundIngress(shardIndex, ingress);
			}, eJobPriority::Control));
	}

	void UdpRouter::DrainBoundIngress(const uint16 shardIndex, const std::shared_ptr<BoundIngressState>& ingress)
	{
		const uint64 drainStartNs = NOW_NS();
		size_t jobPacketCount = 0;
		size_t remaining = ingress->pending.load(std::memory_order_acquire);
		bool timeBudgetReached = false;
		std::vector<BoundIngressState::Item> batch;
		batch.reserve(BoundIngressState::kDrainBatchPacketCount);

		do
		{
			batch.clear();
			const size_t batchLimit = std::min(BoundIngressState::kDrainBatchPacketCount, BoundIngressState::kDrainJobPacketBudget - jobPacketCount);
			const size_t batchCount = ingress->TakeBatch(batch, batchLimit);

			for (auto& item : batch)
			{
				Packet packet = TakeReceivedDatagram(*item.received, static_cast<uint32>(item.numOfBytes));
				if (!packet.IsValid())
					continue;

				auto& state = GetOrCreateSessionShardState(CurrentShardLocalChecked());
				if (auto* forwarded = static_cast<UdpSession*>(state.FindSession(item.sessionId)))
				{
					forwarded->ProcessRecv(item.numOfBytes, std::move(packet), item.ingressRecvTimeNs);
				}
				else
				{
					JAM_LOG_WARN(
						"[UdpRouter] Bound UDP session lookup failed. endpointId={} sessionId={} targetShard={} localShard={}",
						item.endpointId,
						item.sessionId,
						shardIndex,
						CurrentShardLocalChecked().shardIndex);
				}
			}

			jobPacketCount += batchCount;
			remaining = ingress->Complete(batchCount);
			if (remaining == 0 || jobPacketCount == BoundIngressState::kDrainJobPacketBudget)
				break;

			timeBudgetReached = NOW_NS() - drainStartNs >= BoundIngressState::kDrainJobTimeBudgetNs;
		} while (!timeBudgetReached);

		if (remaining > 0)
			ScheduleBoundIngressDrain(shardIndex, ingress);
	}

	void UdpRouter::ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddr, UdpRecvEvent* recvEvent, uint64 ingressRecvTime_ns)
	{
		OwnedUdpRecvEvent received(recvEvent);
		if (numOfBytes == 0 || !m_service || !received)
			return;

		const EndpointId endpointId = Session::MakeEndpointId(remoteAddr);
		UdpIngressRoute ingressRoute = {};

		if (TryGetIngressRoute(endpointId, ingressRoute))
		{
			if (ingressRoute.kind == UdpIngressRouteKind::Admission && ingressRoute.admissionId != 0)
			{
				if (auto admission = m_service->m_admissionContext.lock())
				{
					Packet packet = TakeReceivedDatagram(*received, static_cast<uint32>(numOfBytes));

					admission->OnUdpPacket(AdmissionKey{
						.admissionId = ingressRoute.admissionId,
						.protocol	 = eProtocolType::UDP,
						.endpointId  = endpointId,
					}, std::move(packet));
				}
				return;
			}

			if (ingressRoute.kind == UdpIngressRouteKind::BoundSession && ingressRoute.sessionId != kInvalidSessionId)
			{
				const uint16 targetShardIndex = GetRuntimeShardIndex(ingressRoute.sessionId);
				if (const auto boundShard = GLOBAL_EXEC.GetShardFromIndex(targetShardIndex))
				{
					auto budget = TryAcquireIngressBudget(targetShardIndex);
					if (m_service->GetServiceType() == eServiceType::SERVER && !budget)
						return;

					if (m_service->GetServiceType() == eServiceType::SERVER)
					{
						const auto& ingress = m_boundIngress[targetShardIndex];
						const bool scheduleDrain = ingress->Enqueue(BoundIngressState::Item{
							.numOfBytes			= numOfBytes,
							.ingressRecvTimeNs	= ingressRecvTime_ns,
							.endpointId			= endpointId,
							.sessionId			= ingressRoute.sessionId,
							.received			= std::move(received),
							.lease				= IngressLease(std::move(budget)),
						});

						if (scheduleDrain)
							ScheduleBoundIngressDrain(targetShardIndex, ingress);
					}
					else
					{
						boundShard->Submit(Job([numOfBytes, ingressRecvTime_ns, endpointId, targetShardIndex, sessionId = ingressRoute.sessionId, received = std::move(received)]() mutable
							{
								Packet packet = TakeReceivedDatagram(*received, static_cast<uint32>(numOfBytes));
								if (!packet.IsValid())
									return;

								auto& state = GetOrCreateSessionShardState(CurrentShardLocalChecked());

								if (auto* forwarded = static_cast<UdpSession*>(state.FindSession(sessionId)))
									forwarded->ProcessRecv(numOfBytes, std::move(packet), ingressRecvTime_ns);
								else
									JAM_LOG_WARN("[UdpRouter] Bound UDP session lookup failed. endpointId={} sessionId={} targetShard={} localShard={}",
										endpointId, sessionId, targetShardIndex, CurrentShardLocalChecked().shardIndex);
							}, eJobPriority::Control));
					}
				}
				else
				{
					JAM_LOG_WARN("[UdpRouter] Bound UDP shard resolve failed. endpointId={} sessionId={} targetShard={}",
						endpointId, ingressRoute.sessionId, targetShardIndex);
				}
				return;
			}
		}

		auto service = m_service->shared_from_this();
		if (service->GetServiceType() == eServiceType::SERVER)
		{
			Packet packet = TakeReceivedDatagram(*received, static_cast<uint32>(numOfBytes));
			if (!packet.IsValid())
				return;
			const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
			if (!view.IsValid() || view.TotalSize() != packet->Size() || view.Type() != ePacketType::SYSTEM
				|| ToEnum<eSystemPacketId>(view.Id()) != eSystemPacketId::UDP_BIND_REQ
				|| view.PayloadSize() < sizeof(UDP_BIND_REQ_DATA))
				return;

			const auto* request = reinterpret_cast<const UDP_BIND_REQ_DATA*>(view.Payload());
			if (!request || request->accountId == 0 || request->userId == kInvalidRuntimeId || request->transactionId == 0)
				return;

			auto admission = service->m_admissionContext.lock();
			auto owner = service->MakeUdpSession(remoteAddr);
			if (!admission || !owner)
				return;

			const AdmissionKey key = admission->AddEntry(std::move(owner));
			if (key.admissionId == 0)
				return;

			RegisterIngressAdmission(endpointId, key.admissionId);
			service->NotifyUdpSessionAttached();
			admission->OnUdpPacket(key, std::move(packet));
			return;
		}

		const auto shard = std::static_pointer_cast<ClientService>(service)->GetPrincipalShard();
		if (!shard) return;
		const int32 shardIndex = shard->GetIndex();
		auto budget = shardIndex >= 0 ? TryAcquireIngressBudget(static_cast<uint32>(shardIndex)) : nullptr;
		if (service->GetServiceType() == eServiceType::SERVER && !budget)
			return;

		shard->Submit(Job([service = std::move(service), endpointId, numOfBytes, received = std::move(received), ingressRecvTime_ns, lease = IngressLease(std::move(budget))]() mutable
			{
				auto process = [&]()
					{
						Packet packet = TakeReceivedDatagram(*received, static_cast<uint32>(numOfBytes));
						if (!packet.IsValid())
							return;

						auto clientService = std::static_pointer_cast<ClientService>(service);
						UdpSession* session = clientService->FindUdpSession();
						if (session && session->GetEndpointId() != endpointId)
							session = nullptr;

						if (session)
							session->ProcessRecv(numOfBytes, std::move(packet), ingressRecvTime_ns);
					};
				process();

			}, eJobPriority::Critical));
	}

	void UdpRouter::HandleError(int32 errorCode)
	{
		switch (errorCode)
		{
		case WSAECONNRESET:
		case WSAECONNABORTED:
			break;
		default:
			win_error::LogWsaError("[UdpRouter] socket operation", errorCode);
			break; 
		}
	}

	void UdpRouter::RegisterIngressAdmission(EndpointId endpointId, uint64 admissionId)
	{
		if (admissionId == 0)
		{
			ClearIngressRoute(endpointId);
			return;
		}

		UpsertIngressRoute(endpointId, UdpIngressRouteKind::Admission, admissionId);
	}

	bool UdpRouter::PromoteIngressToBound(EndpointId endpointId, uint64 expectedAdmissionId, SessionId sessionId)
	{
		if (endpointId == kEmptyIngressKey || endpointId == kTombstoneIngressKey
			|| expectedAdmissionId == 0 || sessionId == kInvalidSessionId)
			return false;

		size_t idx = StartIngressIndex(endpointId);
		for (size_t probe = 0; probe < INGRESS_TABLE_CAPACITY; ++probe, idx = (idx + 1) & (INGRESS_TABLE_CAPACITY - 1))
		{
			auto& slot = m_ingressRoutes[idx];
			const uint64 key = slot.key.load(std::memory_order_acquire);
			if (key == kEmptyIngressKey)
				return false;
			if (key != endpointId)
				continue;

			uint32 sequence = slot.sequence.load(std::memory_order_acquire);
			if ((sequence & 1u) != 0 || !slot.sequence.compare_exchange_strong(sequence, sequence + 1, std::memory_order_acq_rel))
			{
				--probe;
				continue;
			}

			const bool matches = slot.key.load(std::memory_order_relaxed) == endpointId
				&& slot.kind.load(std::memory_order_relaxed) == static_cast<uint8>(UdpIngressRouteKind::Admission)
				&& slot.value.load(std::memory_order_relaxed) == expectedAdmissionId;
			if (matches)
			{
				slot.value.store(static_cast<uint64>(sessionId), std::memory_order_relaxed);
				slot.kind.store(static_cast<uint8>(UdpIngressRouteKind::BoundSession), std::memory_order_relaxed);
			}
			slot.sequence.store(sequence + 2, std::memory_order_release);
			return matches;
		}

		return false;
	}

	void UdpRouter::ClearIngressRoute(EndpointId endpointId)
	{
		if (endpointId == kEmptyIngressKey || endpointId == kTombstoneIngressKey)
			return;

		size_t idx = StartIngressIndex(endpointId);
		for (size_t probe = 0; probe < INGRESS_TABLE_CAPACITY; ++probe, idx = (idx + 1) & (INGRESS_TABLE_CAPACITY - 1))
		{
			auto& slot = m_ingressRoutes[idx];
			const uint64 key = slot.key.load(std::memory_order_acquire);
			if (key == kEmptyIngressKey)
				return;
			if (key != endpointId)
				continue;

			uint32 sequence = slot.sequence.load(std::memory_order_acquire);
			if ((sequence & 1u) != 0 ||
				!slot.sequence.compare_exchange_strong(sequence, sequence + 1, std::memory_order_acq_rel))
			{
				--probe;
				continue;
			}

			if (slot.key.load(std::memory_order_acquire) != endpointId)
			{
				slot.sequence.store(sequence + 2, std::memory_order_release);
				--probe;
				continue;
			}

			slot.kind.store(static_cast<uint8>(UdpIngressRouteKind::None), std::memory_order_relaxed);
			slot.value.store(0, std::memory_order_relaxed);
			slot.key.store(kTombstoneIngressKey, std::memory_order_relaxed);
			slot.sequence.store(sequence + 2, std::memory_order_release);
			return;
		}
	}

	bool UdpRouter::ClearIngressAdmission(EndpointId endpointId, uint64 admissionId)
	{
		if (admissionId == 0)
			return false;

		return ClearIngressRouteIfMatches(endpointId, UdpIngressRouteKind::Admission, admissionId);
	}

	bool UdpRouter::ClearIngressBoundRoute(EndpointId endpointId, SessionId sessionId)
	{
		if (sessionId == kInvalidSessionId)
			return false;

		return ClearIngressRouteIfMatches(endpointId, UdpIngressRouteKind::BoundSession, static_cast<uint64>(sessionId));
	}

	bool UdpRouter::ClearIngressRouteIfMatches(EndpointId endpointId, UdpIngressRouteKind kind, uint64 value)
	{
		if (endpointId == kEmptyIngressKey || endpointId == kTombstoneIngressKey)
			return false;

		size_t idx = StartIngressIndex(endpointId);
		for (size_t probe = 0; probe < INGRESS_TABLE_CAPACITY; ++probe, idx = (idx + 1) & (INGRESS_TABLE_CAPACITY - 1))
		{
			auto& slot = m_ingressRoutes[idx];
			const uint64 key = slot.key.load(std::memory_order_acquire);

			if (key == kEmptyIngressKey)
				return false;
			if (key != endpointId)
				continue;

			uint32 sequence = slot.sequence.load(std::memory_order_acquire);
			if ((sequence & 1u) != 0 || !slot.sequence.compare_exchange_strong(sequence, sequence + 1, std::memory_order_acq_rel))
			{
				--probe;
				continue;
			}

			const bool matches = slot.key.load(std::memory_order_relaxed) == endpointId
							  && slot.kind.load(std::memory_order_relaxed) == static_cast<uint8>(kind)
							  && slot.value.load(std::memory_order_relaxed) == value;

			if (!matches)
			{
				slot.sequence.store(sequence + 2, std::memory_order_release);
				return false;
			}

			slot.kind.store(static_cast<uint8>(UdpIngressRouteKind::None), std::memory_order_relaxed);
			slot.value.store(0, std::memory_order_relaxed);
			slot.key.store(kTombstoneIngressKey, std::memory_order_relaxed);
			slot.sequence.store(sequence + 2, std::memory_order_release);

			return true;
		}

		return false;
	}

	bool UdpRouter::TryGetIngressRoute(EndpointId endpointId, UdpIngressRoute& out) const
	{
		if (endpointId == kEmptyIngressKey || endpointId == kTombstoneIngressKey)
			return false;

		size_t idx = StartIngressIndex(endpointId);
		for (size_t probe = 0; probe < INGRESS_TABLE_CAPACITY; ++probe, idx = (idx + 1) & (INGRESS_TABLE_CAPACITY - 1))
		{
			const auto& slot = m_ingressRoutes[idx];
			const uint64 key = slot.key.load(std::memory_order_acquire);
			if (key == kEmptyIngressKey)
				return false;
			if (key != endpointId)
				continue;

			const uint32 sequence = slot.sequence.load(std::memory_order_acquire);
			if ((sequence & 1u) != 0)
			{
				--probe;
				continue;
			}

			const auto kind = static_cast<UdpIngressRouteKind>(slot.kind.load(std::memory_order_relaxed));
			if (kind == UdpIngressRouteKind::None)
				return false;

			const uint64 value = slot.value.load(std::memory_order_relaxed);
			if (slot.sequence.load(std::memory_order_acquire) != sequence || slot.key.load(std::memory_order_acquire) != endpointId)
			{
				--probe;
				continue;
			}

			out.kind        = kind;
			out.admissionId = (kind == UdpIngressRouteKind::Admission) ? value : 0;
			out.sessionId   = (kind == UdpIngressRouteKind::BoundSession) ? static_cast<SessionId>(value) : kInvalidSessionId;

			return true;
		}

		return false;
	}
}
