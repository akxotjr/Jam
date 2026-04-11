#include "pch.h"
#include "jamnet/runtime/ServerNetworkManager.h"

#include "jamnet/runtime/ServerSession.h"
#include "jamnet/runtime/ServerTransportAdapter.h"
#include "jamnet/runtime/world/DefaultWorldAssignmentService.h"

#include "jamnet/sync/networld/ServerNetWorld.h"
#include "jamnet/sync/replication/ReplicationTypes.h"

#include <future>

namespace jam::net
{
	struct ServerNetworkManager::WorldTransferRecord
	{
		enum class ePhase : uint8
		{
			Reserved		= 0,
			LeavingSource	= 1,
			SourceLeft		= 2,
			EnteringTarget	= 3,
			Completed		= 4,
			Failed			= 5,
		};

		WorldId							sourceWorldId	= INVALID_WORLD_ID;
		WorldId							targetWorldId	= INVALID_WORLD_ID;
		uint64							userId			= 0;
		std::atomic<ePhase>			phase			= ePhase::Reserved;
		std::atomic<bool>				completed		= false;
		WorldTransferResult				result			= {};
		std::mutex						callbackMutex;
		std::vector<std::function<void(WorldTransferResult)>> callbacks;

		void AddCallback(std::function<void(WorldTransferResult)> cb)
		{
			if (!cb)
				return;

			if (completed.load(std::memory_order_acquire))
			{
				cb(result);
				return;
			}

			std::scoped_lock guard(callbackMutex);
			if (completed.load(std::memory_order_acquire))
			{
				cb(result);
				return;
			}

			callbacks.push_back(std::move(cb));
		}

		void NotifyAndClose(const WorldTransferResult& finalResult)
		{
			result = finalResult;
			completed.store(true, std::memory_order_release);

			std::vector<std::function<void(WorldTransferResult)>> pending;
			{
				std::scoped_lock guard(callbackMutex);
				pending.swap(callbacks);
			}

			for (auto& cb : pending)
			{
				if (cb)
					cb(result);
			}
		}
	};

	struct WorldTransferAsyncState : public std::enable_shared_from_this<WorldTransferAsyncState>
	{
		ServerNetworkManager*				manager			= nullptr;
		std::shared_ptr<ServerNetworkManager::WorldTransferRecord> record;
		std::shared_ptr<ServerNetWorld>				sourceWorld;
		std::shared_ptr<ServerNetWorld>				targetWorld;
		WorldId										sourceWorldId	= INVALID_WORLD_ID;
		WorldId										targetWorldId	= INVALID_WORLD_ID;
		uint64										userId			= 0;
		std::function<void(WorldTransferResult)>	onDone;
		std::atomic<bool>							completed		= false;

		void Finish(const WorldTransferResult& result)
		{
			if (completed.exchange(true, std::memory_order_acq_rel))
				return;

			if (record && manager)
				manager->CompleteTransfer(userId, result);
			else if (onDone)
				onDone(result);
		}

		void FailAndRollback(eWorldTransferReason reason, bool reenterSource = false)
		{
			if (manager)
			{
				manager->UpdateTransferPhase(userId, static_cast<uint8>(ServerNetworkManager::WorldTransferRecord::ePhase::Failed));
				manager->RollbackWorldTransferMembership(sourceWorldId, targetWorldId, userId);
			}

			const WorldTransferResult failedResult
			{
				.outcome		= eWorldTransferOutcome::Failed,
				.reason			= reason,
				.sourceWorldId	= sourceWorldId,
				.targetWorldId	= targetWorldId,
			};

			if (!reenterSource || !sourceWorld)
			{
				if (manager)
					manager->TryDestroyWorldIfEmpty(targetWorldId);

				Finish(failedResult);
				return;
			}

			auto self = shared_from_this();
			if (!sourceWorld->Enter(userId, [self]() mutable
				{
					if (self->manager)
						self->manager->TryDestroyWorldIfEmpty(self->targetWorldId);

					self->Finish(WorldTransferResult
						{
							.outcome		= eWorldTransferOutcome::Failed,
							.reason			= eWorldTransferReason::MailboxClosed,
							.sourceWorldId	= self->sourceWorldId,
							.targetWorldId	= self->targetWorldId,
						});
				}))
			{
				if (manager)
					manager->TryDestroyWorldIfEmpty(targetWorldId);

				Finish(failedResult);
			}
		}

		void BeginTargetEnter()
		{
			if (manager)
				manager->UpdateTransferPhase(userId, static_cast<uint8>(ServerNetworkManager::WorldTransferRecord::ePhase::EnteringTarget));

			auto self = shared_from_this();
			if (!targetWorld->Enter(userId, [self]() mutable
				{
					if (self->manager)
						self->manager->UpdateTransferPhase(self->userId, static_cast<uint8>(ServerNetworkManager::WorldTransferRecord::ePhase::Completed));

					if (self->manager && self->sourceWorldId != INVALID_WORLD_ID)
						self->manager->TryDestroyWorldIfEmpty(self->sourceWorldId);

					self->Finish(WorldTransferResult
						{
							.outcome		= eWorldTransferOutcome::Succeeded,
							.reason			= eWorldTransferReason::None,
							.sourceWorldId	= self->sourceWorldId,
							.targetWorldId	= self->targetWorldId,
						});
				}))
			{
				FailAndRollback(eWorldTransferReason::MailboxClosed, sourceWorld != nullptr);
			}
		}

		void Begin()
		{
			if (!sourceWorld)
			{
				BeginTargetEnter();
				return;
			}

			if (manager)
				manager->UpdateTransferPhase(userId, static_cast<uint8>(ServerNetworkManager::WorldTransferRecord::ePhase::LeavingSource));

			auto self = shared_from_this();
			if (!sourceWorld->Leave(userId, [self]() mutable
				{
					if (self->manager)
						self->manager->UpdateTransferPhase(self->userId, static_cast<uint8>(ServerNetworkManager::WorldTransferRecord::ePhase::SourceLeft));

					self->BeginTargetEnter();
				}))
			{
				FailAndRollback(eWorldTransferReason::MailboxClosed, false);
			}
		}
	};

	ServerNetworkManager::ServerNetworkManager(const ServerConfig& config)
		: m_config(config)
	{
		m_tranportAdapter = std::make_shared<ServerTransportAdapter>();

		if (m_tranportAdapter)
			m_tranportAdapter->SetNetworkManager(this);

		SetWorldAssignmentService(std::make_unique<DefaultWorldAssignmentService>());
	}

	ServerNetworkManager::~ServerNetworkManager()
	{
		Stop();
	}

	bool ServerNetworkManager::Start()
	{
		if (m_running.load(std::memory_order_acquire))
			return true;

		if (!StartServerService())
			return false;

		m_running.store(true, std::memory_order_release);

		JAMNET_LOG_INFO("ServerNetworkManager started successfully");
		return true;
	}

	void ServerNetworkManager::Stop()
	{
		if (!m_running.exchange(false, std::memory_order_acq_rel))
			return;

		std::vector<std::shared_ptr<ServerNetWorld>> victims;
		std::vector<std::shared_ptr<WorldTransferRecord>> transfers;

		{
			WRITE_LOCK
			victims.reserve(m_worlds.size());
			for (auto& world : m_worlds | std::views::values)
			{
				if (world)
					victims.push_back(std::move(world));
			}

			m_tcpSessions.clear();
			m_udpSessions.clear();
			m_worldMembers.clear();
			m_worlds.clear();
			transfers.reserve(m_transfers.size());
			for (auto& transfer : m_transfers | std::views::values)
			{
				if (transfer)
					transfers.push_back(std::move(transfer));
			}
			m_transfers.clear();
			m_recentTransferResults.clear();
			m_worldDirectory = {};
		}

		for (auto& victim : victims)
		{
			if (victim)
				victim->BeginShutdown(eMailboxCloseMode::Abort);
		}

		for (auto& transfer : transfers)
		{
			if (!transfer)
				continue;

			transfer->phase.store(WorldTransferRecord::ePhase::Failed, std::memory_order_release);
			transfer->NotifyAndClose(WorldTransferResult
				{
					.outcome		= eWorldTransferOutcome::Failed,
					.reason			= eWorldTransferReason::Shutdown,
					.sourceWorldId	= transfer->sourceWorldId,
					.targetWorldId	= transfer->targetWorldId,
				});
		}

		if (m_tranportAdapter)
			m_tranportAdapter.reset();

		StopServerService();

		if (m_assignmentService)
		{
			m_assignmentService->Init(nullptr);
			m_assignmentService.reset();
		}

		JAMNET_LOG_INFO("ServerNetworkManager stopped");
	}

	void ServerNetworkManager::SetWorldAssignmentService(std::unique_ptr<IWorldAssignmentService> service)
	{
		if (m_assignmentService)
			m_assignmentService->Init(nullptr);

		m_assignmentService = std::move(service);

		if (m_assignmentService)
			m_assignmentService->Init(this);
	}

	WorldAssignmentResult ServerNetworkManager::RequestWorldAssignment(const WorldAssignmentRequest& req)
	{
		if (!m_assignmentService)
			return {};

		return m_assignmentService->AssignPrincipal(req);
	}

	void ServerNetworkManager::SetWorldAssignmentPolicy(std::unique_ptr<IWorldAssignmentPolicy> policy)
	{
		if (!m_assignmentService)
			SetWorldAssignmentService(std::make_unique<DefaultWorldAssignmentService>());

		if (m_assignmentService)
			m_assignmentService->SetWorldAssignmentPolicy(std::move(policy));
	}

	IWorldAssignmentPolicy* ServerNetworkManager::GetWorldAssignmentPolicy() const
	{
		return m_assignmentService ? m_assignmentService->GetWorldAssignmentPolicy() : nullptr;
	}

	WorldId ServerNetworkManager::ResolveWorldId(const WorldKey& key)
	{
		if (!key.IsValid())
			return INVALID_WORLD_ID;

		READ_LOCK
		return m_worldDirectory.FindWorldId(key);
	}

	WorldId ServerNetworkManager::ResolveOrAllocateWorldId(const WorldKey& key, const WorldOptions& options)
	{
		if (!key.IsValid())
			return INVALID_WORLD_ID;

		WRITE_LOCK
		return m_worldDirectory.FindOrAddWorld(key, options);
	}

	WorldKey ServerNetworkManager::GetWorldKey(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return INVALID_WORLD_KEY;

		READ_LOCK
		return m_worldDirectory.FindWorldKey(worldId);
	}

	ServerNetWorld* ServerNetworkManager::GetWorld(WorldId worldId)
	{
		READ_LOCK
		auto it = m_worlds.find(worldId);
		return (it != m_worlds.end()) ? it->second.get() : nullptr;
	}

	ServerNetWorld* ServerNetworkManager::GetWorld(const WorldKey& key)
	{
		return GetWorld(ResolveWorldId(key));
	}

	ServerNetWorld* ServerNetworkManager::GetOrCreateWorld(WorldId worldId)
	{
		return GetOrCreateWorld(worldId, GetWorldOptions(worldId));
	}

	ServerNetWorld* ServerNetworkManager::GetOrCreateWorld(WorldId worldId, const WorldOptions& options)
	{
		if (worldId == INVALID_WORLD_ID)
			return nullptr;

		WRITE_LOCK
		m_worldDirectory.SetWorldOptions(worldId, options);

		auto& slot = m_worlds[worldId];
		if (!slot)
		{
			slot = std::make_shared<ServerNetWorld>();
			slot->SetWorldId(worldId);
			slot->SetTransportAdapter(m_tranportAdapter.get());

			if (m_config.physicsFactory)
			{
				if (auto phys = m_config.physicsFactory())
					slot->SetPhysicsFacade(std::move(phys));
			}

			slot->SetLevelPath(m_config.levelPath);
			slot->Init();
			slot->Tick(SIMULATION_TICK_NS);
		}

		return slot.get();
	}

	ServerNetWorld* ServerNetworkManager::GetOrCreateWorld(const WorldKey& key, const WorldOptions& options)
	{
		return GetOrCreateWorld(ResolveOrAllocateWorldId(key, options), options);
	}

	void ServerNetworkManager::DestroyWorld(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return;

		std::shared_ptr<ServerNetWorld> victim;
		{
			WRITE_LOCK
			auto it = m_worlds.find(worldId);
			if (it == m_worlds.end())
				return;

			victim = std::move(it->second);
			m_worlds.erase(it);
			m_worldMembers.erase(worldId);
			m_worldDirectory.RemoveWorld(worldId);
		}

		if (victim)
			victim->BeginShutdown(eMailboxCloseMode::Drain);
	}

	void ServerNetworkManager::DestroyWorld(const WorldKey& key)
	{
		DestroyWorld(ResolveWorldId(key));
	}

	void ServerNetworkManager::RegisterTcpSession(uint64 userId, const std::shared_ptr<ServerTcpSession>& tcp)
	{
		if (!userId || !tcp)
			return;

		WRITE_LOCK
		m_tcpSessions[userId] = tcp;

		JAMNET_LOG_INFO("UserId = {}] TCP Session registered", userId);
	}

	void ServerNetworkManager::RegisterUdpSession(uint64 userId, const std::shared_ptr<ServerUdpSession>& udp)
	{
		if (!userId || !udp)
			return;

		WRITE_LOCK
		m_udpSessions[userId] = udp;

		JAMNET_LOG_INFO("UserId = {}] UDP Session registered", userId);
	}

	void ServerNetworkManager::UnregisterSession(uint64 userId)
	{
		if (!userId)
			return;

		std::vector<WorldId> worldsToNotify;
		{
			WRITE_LOCK
			m_tcpSessions.erase(userId);
			m_udpSessions.erase(userId);

			for (auto& [worldId, members] : m_worldMembers)
			{
				if (std::erase(members, userId) > 0)
					worldsToNotify.push_back(worldId);
			}
		}

		for (WorldId worldId : worldsToNotify)
		{
			if (auto* world = GetWorld(worldId))
				world->Leave(userId);

			TryDestroyWorldIfEmpty(worldId);
		}

		JAMNET_LOG_INFO("UserId = {}] Session unregistered", userId);
	}

	std::shared_ptr<ServerTcpSession> ServerNetworkManager::FindTcpSession(uint64 userId)
	{
		READ_LOCK
		auto it = m_tcpSessions.find(userId);
		return (it != m_tcpSessions.end()) ? it->second : nullptr;
	}

	std::shared_ptr<ServerUdpSession> ServerNetworkManager::FindUdpSession(uint64 userId)
	{
		READ_LOCK
		auto it = m_udpSessions.find(userId);
		return (it != m_udpSessions.end()) ? it->second : nullptr;
	}

	void ServerNetworkManager::BroadcastPacket(const std::shared_ptr<SendBuffer>& buf, eProtocolType protocol)
	{
		if (!buf)
			return;

		READ_LOCK

		if (protocol == eProtocolType::TCP)
		{
			for (auto& session : m_tcpSessions | std::views::values)
			{
				if (session && session->IsConnected())
					session->Send(buf);
			}
		}

		if (protocol == eProtocolType::UDP)
		{
			for (auto& session : m_udpSessions | std::views::values)
			{
				if (session && session->IsConnected())
					session->Send(buf);
			}
		}
	}

	void ServerNetworkManager::SendToUser(uint64 userId, const std::shared_ptr<SendBuffer>& buf, eProtocolType protocol)
	{
		if (!buf)
			return;

		if (protocol == eProtocolType::TCP)
		{
			if (auto tcp = FindTcpSession(userId))
			{
				if (tcp->IsConnected())
					tcp->Send(buf);
			}
		}

		if (protocol == eProtocolType::UDP)
		{
			if (auto udp = FindUdpSession(userId))
			{
				if (udp->IsConnected())
					udp->Send(buf);
			}
		}
	}

	void ServerNetworkManager::JoinWorld(WorldId worldId, uint64 userId)
	{
		if (worldId == INVALID_WORLD_ID || userId == 0)
			return;

		WRITE_LOCK
		auto& members = m_worldMembers[worldId];
		if (std::ranges::find(members, userId) == members.end())
			members.push_back(userId);
	}

	void ServerNetworkManager::LeaveWorld(WorldId worldId, uint64 userId)
	{
		if (worldId == INVALID_WORLD_ID || userId == 0)
			return;

		{
			WRITE_LOCK
			auto it = m_worldMembers.find(worldId);
			if (it == m_worldMembers.end())
				return;

			std::erase(it->second, userId);
			if (!it->second.empty())
				return;

			m_worldMembers.erase(it);
		}

		TryDestroyWorldIfEmpty(worldId);
	}

	bool ServerNetworkManager::TransferWorldAsync(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId, std::function<void(WorldTransferResult)> onDone)
	{
		if (targetWorldId == INVALID_WORLD_ID || userId == 0)
			return false;

		if (sourceWorldId == targetWorldId)
		{
			if (onDone)
				onDone(WorldTransferResult
					{
						.outcome		= eWorldTransferOutcome::Succeeded,
						.reason			= eWorldTransferReason::AlreadyInTarget,
						.sourceWorldId	= sourceWorldId,
						.targetWorldId	= targetWorldId,
					});
			return true;
		}

		const WorldOptions targetOptions = GetWorldOptions(targetWorldId);
		if (!GetOrCreateWorld(targetWorldId, targetOptions))
			return false;

		const auto targetWorld = FindWorldShared(targetWorldId);
		const auto sourceWorld = FindWorldShared(sourceWorldId);
		if (!targetWorld)
			return false;

		bool shouldStart = false;
		[[maybe_unused]] WorldTransferResult immediateResult{};
		const auto record = BeginOrJoinTransfer(sourceWorldId, targetWorldId, userId, std::move(onDone), shouldStart, immediateResult);
		if (!record)
			return false;

		if (!shouldStart)
			return true;

		if (!ReserveWorldTransferMembership(sourceWorldId, targetWorldId, userId, targetOptions))
		{
			CompleteTransfer(userId, WorldTransferResult
				{
					.outcome		= eWorldTransferOutcome::Failed,
					.reason			= eWorldTransferReason::CapacityExceeded,
					.sourceWorldId	= sourceWorldId,
					.targetWorldId	= targetWorldId,
				});
			return false;
		}

		auto state = std::make_shared<WorldTransferAsyncState>();
		state->manager       = this;
		state->record		 = record;
		state->sourceWorld   = sourceWorld;
		state->targetWorld   = targetWorld;
		state->sourceWorldId = sourceWorldId;
		state->targetWorldId = targetWorldId;
		state->userId        = userId;
		state->Begin();
		return true;
	}

	WorldTransferResult ServerNetworkManager::TransferWorldAwait(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId, uint64 timeout_ns)
	{
		auto* shard = SHARD_LOCAL_CURRENT();
		auto* sched = shard ? shard->scheduler : nullptr;
		if (!sched || sched->Current() == 0)
			return TransferWorld(sourceWorldId, targetWorldId, userId);

		const FiberAwaitKey awaitKey = m_nextTransferAwaitKey.fetch_add(1, std::memory_order_relaxed);
		auto result = std::make_shared<WorldTransferResult>(WorldTransferResult
			{
				.outcome		= eWorldTransferOutcome::Failed,
				.reason			= eWorldTransferReason::None,
				.sourceWorldId	= sourceWorldId,
				.targetWorldId	= targetWorldId,
			});

		if (!TransferWorldAsync(sourceWorldId, targetWorldId, userId,
			[sched, awaitKey, result](WorldTransferResult transferResult) mutable
			{
				*result = transferResult;
				sched->PostResume(awaitKey);
			}))
		{
			return WorldTransferResult
				{
					.outcome		= eWorldTransferOutcome::Failed,
					.reason			= eWorldTransferReason::ConflictingTransfer,
					.sourceWorldId	= sourceWorldId,
					.targetWorldId	= targetWorldId,
				};
		}

		const uint64 deadline_ns = timeout_ns ? (NOW_NS() + timeout_ns) : 0;
		if (!sched->Suspend(awaitKey, deadline_ns))
		{
			return WorldTransferResult
				{
					.outcome		= eWorldTransferOutcome::InDoubt,
					.reason			= eWorldTransferReason::Timeout,
					.sourceWorldId	= sourceWorldId,
					.targetWorldId	= targetWorldId,
				};
		}

		return *result;
	}

	WorldTransferResult ServerNetworkManager::TransferWorld(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId)
	{
		auto* shard = SHARD_LOCAL_CURRENT();
		auto* sched = shard ? shard->scheduler : nullptr;
		if (sched && sched->Current() != 0)
			return TransferWorldAwait(sourceWorldId, targetWorldId, userId);

		auto done = std::make_shared<std::promise<WorldTransferResult>>();
		auto future = done->get_future();

		if (!TransferWorldAsync(sourceWorldId, targetWorldId, userId,
			[done](WorldTransferResult result) mutable
			{
				done->set_value(result);
			}))
		{
			return WorldTransferResult
				{
					.outcome		= eWorldTransferOutcome::Failed,
					.reason			= eWorldTransferReason::ConflictingTransfer,
					.sourceWorldId	= sourceWorldId,
					.targetWorldId	= targetWorldId,
				};
		}

		return future.get();
	}

	bool ServerNetworkManager::AttachTransferCallback(uint64 userId, std::function<void(WorldTransferResult)> onDone)
	{
		if (userId == 0 || !onDone)
			return false;

		std::shared_ptr<WorldTransferRecord> record;
		std::optional<WorldTransferResult> completedResult;
		{
			READ_LOCK
			if (auto it = m_transfers.find(userId); it != m_transfers.end())
			{
				record = it->second;
			}
			else if (auto doneIt = m_recentTransferResults.find(userId); doneIt != m_recentTransferResults.end())
			{
				completedResult = doneIt->second;
			}
		}

		if (record)
		{
			record->AddCallback(std::move(onDone));
			return true;
		}

		if (completedResult.has_value())
		{
			onDone(*completedResult);
			return true;
		}

		return false;
	}

	uint32 ServerNetworkManager::GetWorldMemberCount(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return 0;

		READ_LOCK
		auto it = m_worldMembers.find(worldId);
		return (it != m_worldMembers.end()) ? static_cast<uint32>(it->second.size()) : 0;
	}

	WorldOptions ServerNetworkManager::GetWorldOptions(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return {};

		READ_LOCK
		return m_worldDirectory.FindWorldOptions(worldId);
	}

	void ServerNetworkManager::EnumerateConnectedUsers(const std::function<void(uint64)>& fn)
	{
		if (!fn)
			return;

		std::vector<uint64> users;
		{
			READ_LOCK
			users.reserve(m_tcpSessions.size());
			for (const auto& uid : m_tcpSessions | std::views::keys)
				users.push_back(uid);
		}

		for (uint64 uid : users)
			fn(uid);
	}

	void ServerNetworkManager::EnumerateWorldUsers(WorldId worldId, const std::function<void(uint64)>& fn)
	{
		if (!fn || worldId == INVALID_WORLD_ID)
			return;

		std::vector<uint64> users;
		{
			READ_LOCK
			auto it = m_worldMembers.find(worldId);
			if (it == m_worldMembers.end())
				return;

			users = it->second;
		}

		for (uint64 uid : users)
			fn(uid);
	}

	void ServerNetworkManager::TryDestroyWorldIfEmpty(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return;

		const uint32 memberCount = GetWorldMemberCount(worldId);
		if (memberCount != 0)
			return;

		const WorldOptions options = GetWorldOptions(worldId);
		if (!options.destroyWhenEmpty || options.persistent)
			return;

		DestroyWorld(worldId);
	}

	void ServerNetworkManager::TryDestroyWorldIfEmpty(const WorldKey& key)
	{
		TryDestroyWorldIfEmpty(ResolveWorldId(key));
	}

	std::shared_ptr<ServerNetworkManager::WorldTransferRecord> ServerNetworkManager::BeginOrJoinTransfer(
		WorldId sourceWorldId, WorldId targetWorldId, uint64 userId, std::function<void(WorldTransferResult)> onDone, bool& shouldStart, WorldTransferResult& immediateResult)
	{
		shouldStart = false;
		immediateResult = WorldTransferResult
			{
				.outcome		= eWorldTransferOutcome::Failed,
				.reason			= eWorldTransferReason::None,
				.sourceWorldId	= sourceWorldId,
				.targetWorldId	= targetWorldId,
			};

		std::shared_ptr<WorldTransferRecord> record;
		{
			WRITE_LOCK

			auto it = m_transfers.find(userId);
			if (it != m_transfers.end())
			{
				record = it->second;
				if (!record || record->completed.load(std::memory_order_acquire))
				{
					m_transfers.erase(it);
					record.reset();
				}
				else if (record->sourceWorldId != sourceWorldId || record->targetWorldId != targetWorldId)
				{
					immediateResult.reason = eWorldTransferReason::ConflictingTransfer;
					return nullptr;
				}
			}

			if (!record)
			{
				record = std::make_shared<WorldTransferRecord>();
				record->sourceWorldId = sourceWorldId;
				record->targetWorldId = targetWorldId;
				record->userId = userId;
				m_transfers[userId] = record;
				m_recentTransferResults.erase(userId);
				shouldStart = true;
			}
		}

		record->AddCallback(std::move(onDone));
		return record;
	}

	void ServerNetworkManager::UpdateTransferPhase(uint64 userId, uint8 phase)
	{
		std::shared_ptr<WorldTransferRecord> record;
		{
			READ_LOCK
			auto it = m_transfers.find(userId);
			if (it == m_transfers.end())
				return;

			record = it->second;
		}

		if (record)
			record->phase.store(static_cast<WorldTransferRecord::ePhase>(phase), std::memory_order_release);
	}

	void ServerNetworkManager::CompleteTransfer(uint64 userId, const WorldTransferResult& result)
	{
		std::shared_ptr<WorldTransferRecord> record;
		{
			WRITE_LOCK
			auto it = m_transfers.find(userId);
			if (it == m_transfers.end())
			{
				m_recentTransferResults[userId] = result;
				return;
			}

			record = it->second;
			m_transfers.erase(it);
			m_recentTransferResults[userId] = result;
		}

		if (!record)
			return;

		record->phase.store(result.Succeeded() ? WorldTransferRecord::ePhase::Completed : WorldTransferRecord::ePhase::Failed, std::memory_order_release);
		record->NotifyAndClose(result);
	}

	std::shared_ptr<ServerNetWorld> ServerNetworkManager::FindWorldShared(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return nullptr;

		READ_LOCK
		auto it = m_worlds.find(worldId);
		return (it != m_worlds.end()) ? it->second : nullptr;
	}

	bool ServerNetworkManager::ReserveWorldTransferMembership(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId, const WorldOptions& targetOptions)
	{
		WRITE_LOCK

		auto& targetMembers = m_worldMembers[targetWorldId];
		const bool alreadyInTarget = std::ranges::find(targetMembers, userId) != targetMembers.end();

		if (!alreadyInTarget)
		{
			if (targetOptions.capacity != 0 && targetMembers.size() >= targetOptions.capacity)
				return false;

			targetMembers.push_back(userId);
		}

		if (sourceWorldId != INVALID_WORLD_ID)
		{
			if (auto sourceIt = m_worldMembers.find(sourceWorldId); sourceIt != m_worldMembers.end())
			{
				std::erase(sourceIt->second, userId);
				if (sourceIt->second.empty())
					m_worldMembers.erase(sourceIt);
			}
		}

		return true;
	}

	void ServerNetworkManager::RollbackWorldTransferMembership(WorldId sourceWorldId, WorldId targetWorldId, uint64 userId)
	{
		WRITE_LOCK

		if (auto targetIt = m_worldMembers.find(targetWorldId); targetIt != m_worldMembers.end())
		{
			std::erase(targetIt->second, userId);
			if (targetIt->second.empty())
				m_worldMembers.erase(targetIt);
		}

		if (sourceWorldId != INVALID_WORLD_ID)
		{
			auto& sourceMembers = m_worldMembers[sourceWorldId];
			if (std::ranges::find(sourceMembers, userId) == sourceMembers.end())
				sourceMembers.push_back(userId);
		}
	}

	bool ServerNetworkManager::StartServerService()
	{
		ServiceConfig cfg{};
		cfg.localTcpAddress		= m_config.tcpAddress;
		cfg.localUdpAddress		= m_config.udpAddress;
		cfg.maxTcpSessionCount	= m_config.maxConnections;
		cfg.maxUdpSessionCount	= m_config.maxConnections;

		m_service = std::make_shared<ServerService>(cfg);
		if (!m_service)
			return false;

		m_service->SetSessionFactory<ServerTcpSession, ServerUdpSession>();
		m_service->SetSessionInitCallback([this](const std::shared_ptr<Session>& session)
			{
				if (auto tcp = dynamic_pointer_cast<ServerTcpSession>(session))
				{
					tcp->SetNetworkManager(this);
					RPCRegisterRequest<fb::fbTcpBindReqT>(tcp, tcp.get(), &ServerTcpSession::OnTcpBindRequest);
				}
				else if (auto udp = dynamic_pointer_cast<ServerUdpSession>(session))
				{
					udp->SetNetworkManager(this);
					RPCRegisterRequest<fb::fbUdpBindReqT>(udp, udp.get(), &ServerUdpSession::OnUdpBindRequest);
					RPCRegisterRequest<fb::fbRequestWorldAssignmentReqT>(udp, udp.get(), &ServerUdpSession::OnRequestWorldAssignmentReq);
					RPCRegisterRequest<fb::fbSpawnActorReqT>(udp, udp.get(), &ServerUdpSession::OnSpawnActorRequest);
					RPCRegisterRequest<fb::fbDespawnActorReqT>(udp, udp.get(), &ServerUdpSession::OnDespawnActorRequest);
					RPCRegisterRequest<fb::fbPossessActorReqT>(udp, udp.get(), &ServerUdpSession::OnPossessActorRequest);
					RPCRegisterRequest<fb::fbUnpossessActorReqT>(udp, udp.get(), &ServerUdpSession::OnUnpossessActorRequest);
				}
			});

		m_service->Init();

		if (!m_service->Start())
			return false;

		return true;
	}

	void ServerNetworkManager::StopServerService()
	{
		if (m_service)
		{
			m_service->CloseService();
			m_service.reset();
		}
	}
}
