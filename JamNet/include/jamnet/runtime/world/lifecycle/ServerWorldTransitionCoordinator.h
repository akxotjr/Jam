#pragma once

#include "jamnet/runtime/application/ServerNetworkManager.h"
#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/world/lifecycle/WorldDirectory.h"
#include "jamnet/runtime/world/data/WorldInstanceDatabase.h"
#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"
#include "jamnet/runtime/world/data/WorldTemplateDatabase.h"
#include "jamnet/runtime/world/data/WorldConfigResolver.h"
#include "jamnet/runtime/world/actor/ActorArchetypeDatabase.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>


namespace jam::net
{
	class ServerNetworkManager;
	class WorldBase;
	struct WorldShardState;

	// Server-owned reducer for the only PhysicalWorld transition contract:
	// Enter or Leave. It directly owns the user/world-shard orchestration path.
	class ServerWorldTransitionCoordinator : public std::enable_shared_from_this<ServerWorldTransitionCoordinator>
	{
	public:
		using SendPrepareFn = std::function<void(uint64, const ClientWorldPrepare&)>;
		using SendCommitFn = std::function<void(uint64, const ClientWorldCommit&)>;
		using TransitionResultFn = std::function<void(uint64, const WorldTransitionResult&)>;
		using MainChangedFn = std::function<void(uint64, const UserPhysicalWorldState&)>;

		bool					Initialize(ServerNetworkManager* owner);
		void					Shutdown();
		void					BootstrapConfiguredWorlds(std::function<void(bool)> completed);
		void					DestroyRuntime(const WorldRuntimeRef& runtime);
		bool					SubmitWorldJob(const WorldRuntimeRef& runtime, std::function<void(WorldBase&)> job);
		const WorldDirectory&	GetDirectory() const { return m_directory; }
		void					SetTransport(SendPrepareFn prepare, SendCommitFn commit, TransitionResultFn result, MainChangedFn changed);
		void					Enter(uint64 userId, const EnterWorldRequest& request, uint64 nowNs);
		void					Leave(uint64 userId, const LeaveWorldRequest& request);
		void					OnClientBarrierResult(uint64 userId, const ClientBarrierResult& result, uint64 nowNs);
		void					Tick(uint64 nowNs);
		void					OnDisconnected(uint64 userId);

	private:
		UserContext*							FindUserOnCurrentShard(uint64 userId) const;
		UserContext*							FindExpectedTransition(const WorldTransitionContinuation& continuation) const;
		bool									SubmitToUserShard(uint64 userId, std::function<void()> job);
		std::optional<UserPhysicalWorldState>	CommitMain(uint64 userId, WorldStateRevision expectedRevision, std::optional<WorldRuntimeRef> main);
		WorldTransitionToken					IssueTransitionToken();
		void									SendResult(uint64 userId, const WorldTransitionResult& result) const;
		std::optional<WorldInstanceRef>			ResolveDestination(const EnterWorldRequest& request) const;
		void									EnterOnUserShard(uint64 userId, const EnterWorldRequest& request, uint64 nowNs);
		using RuntimeReadyFn = std::function<void(std::optional<WorldRuntimeRef>)>;
		void								EnsureRuntimeAsync(const WorldInstanceRef& instance, RuntimeReadyFn completed);
		void								CompleteRuntimeCreation(const WorldInstanceRef& instance, std::optional<WorldRecord> record);
		void								OnRuntimeReady(const WorldTransitionContinuation& continuation, std::optional<WorldRuntimeRef> runtime);

		bool SubmitWorldCommand(const WorldRuntimeRef& runtime, const WorldTransitionContinuation& continuation,
			std::function<bool(WorldShardState&)> command);
		void OnWorldCommandCompleted(const WorldTransitionContinuation& continuation, bool succeeded);
		bool SubmitReserveTarget(UserContext& user);
		bool SubmitDetachSource(UserContext& user);
		bool SubmitAttachTarget(UserContext& user);
		bool SubmitPrepareTargetContent(UserContext& user);
		bool SubmitActivateTarget(UserContext& user);
		bool SubmitCommitSourceLeave(UserContext& user);
		bool SubmitRollbackTarget(UserContext& user);
		bool SubmitRestoreSource(UserContext& user);
		void SendClientPrepare(UserContext& user);
		void SendClientCommit(UserContext& user);
		void BeginFailure(UserContext& user, eWorldTransitionFailure failure, bool commandInFlight = false);
		void FinishFailure(UserContext& user);
		void CompleteEnter(UserContext& user, const UserPhysicalWorldState& state);
		void CompleteLeave(UserContext& user, const UserPhysicalWorldState& state);
		void CompleteDisconnectedWorldLeave(uint64 userId, const WorldRuntimeRef& runtime);

	private:

		std::atomic<uint64> m_nextTransitionToken{ 1 };
		uint64 m_barrierTimeoutNs = 15'000'000'000ull;

		ServerNetworkManager*				 m_owner = nullptr;
		WorldDirectory						 m_directory;
		WorldTemplateDatabase				 m_templatesDB;
		WorldArchetypeDatabase				 m_archetypesDB;
		WorldInstanceDatabase				 m_instancesDB;
		ActorArchetypeDatabase				 m_actorArchetypesDB;
		std::unique_ptr<WorldConfigResolver> m_configResolver;

		std::mutex						m_runtimeCreationMutex;
		struct RuntimeWaiter
		{
			RuntimeReadyFn	completed;
		};
		std::unordered_map<uint64, std::vector<RuntimeWaiter>> m_runtimeWaiters;

		SendPrepareFn					m_sendPrepare;
		SendCommitFn					m_sendCommit;
		TransitionResultFn				m_transitionResult;
		MainChangedFn					m_mainChanged;
	};
}
