#pragma once

#include "jamnet/runtime/application/ServerNetworkManager.h"
#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/world/lifecycle/WorldShardState.h"
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
#include <variant>
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

		bool					Initialize(ServerNetworkManager* owner);
		void					Close();
		void					BootstrapConfiguredWorlds(std::function<void(bool)> completed);
		void					DestroyWorld(const WorldRef& world);
		
		void					Enter(UserId userId, const EnterWorldRequest& request, uint64 nowNs);
		void					Leave(UserId userId, const LeaveWorldRequest& request);
		
		void					OnClientWorldSyncResult(UserId userId, const ClientWorldSyncResult& result, uint64 nowNs);
		
		void					Tick(uint64 nowNs);
		
		void					OnDisconnected(UserId userId);
		void					OnReconnected(UserId userId);
		void					OnSessionChanged(UserId userId);

	private:
		using WorldReadyFn = std::function<void(std::optional<WorldRef>)>;

		struct WorldReadyEvent
		{
			WorldTransitionContinuation		continuation;
			std::optional<WorldRef>	world;
		};
		struct WorldCommandCompletedEvent
		{
			WorldTransitionContinuation	continuation;
			bool						succeeded = false;
		};
		struct ClientWorldSyncCompletedEvent
		{
			UserId				userId = {};
			ClientWorldSyncResult result;
			uint64				nowNs = 0;
		};
		struct TransitionTimedOutEvent
		{
			WorldTransitionContinuation	continuation;
			uint64						nowNs = 0;
		};
		struct UserDisconnectedEvent
		{
			WorldTransitionContinuation continuation;
		};
		using WorldTransitionEvent = std::variant<
			WorldReadyEvent, 
			WorldCommandCompletedEvent, 
			ClientWorldSyncCompletedEvent,
			TransitionTimedOutEvent, 
			UserDisconnectedEvent>;

		enum class eWorldSlotState : uint8
		{
			Creating = 0,
			Ready,
			Destroying,
		};

		struct WorldSlot
		{
			WorldInstanceRef				instance;
			eWorldSlotState					state = eWorldSlotState::Creating;
			std::optional<WorldRef>			world;
			std::vector<WorldReadyFn>		waiters;
		};



		UserContext*						FindUserOnCurrentShard(UserId userId) const;
		UserContext*						FindExpectedTransition(const WorldTransitionContinuation& continuation) const;
		std::optional<UserWorldState>		CommitMain(UserId userId, WorldStateRevision expectedRevision, std::optional<WorldRef> main);
		
		WorldTransitionState&				BeginTransition(UserContext& user, WorldTransitionState transition, eWorldTransitionPhase initialPhase);
		bool								AdvanceTransition(UserContext& user, eWorldTransitionPhase nextPhase);
		void								FailTransition(UserContext& user, eWorldTransitionFailure failure, bool commandInFlight = false);
		void								ReduceTransition(const WorldTransitionEvent& event);
		
		WorldTransitionToken				IssueTransitionToken();
		WorldSyncToken						IssueWorldSyncToken();
		
		std::optional<WorldInstanceRef>		ResolveDestination(const EnterWorldRequest& request) const;
		
		void								EnterOnUserShard(UserId userId, const EnterWorldRequest& request, uint64 nowNs);
		void								LeaveOnUserShard(UserId userId, const LeaveWorldRequest& request);
		
		void								EnsureWorldAsync(const WorldInstanceRef& instance, WorldReadyFn completed);
		void								StartWorldCreation(const WorldInstanceRef& instance);
		void								CompleteWorldCreation(const WorldInstanceRef& instance, std::optional<WorldRecord> record);
		void								CompleteWorldDestruction(const WorldRef& world, bool destroyed);
		void								OnWorldReady(const WorldTransitionContinuation& continuation, std::optional<WorldRef> world);

		bool								SubmitWorldCommand(const WorldRef& world, const WorldTransitionContinuation& continuation, std::function<bool(WorldShardState&)> command);
		void								OnWorldCommandCompleted(const WorldTransitionContinuation& continuation, bool succeeded);
		
		void								FinishFailure(UserContext& user);
		
		void								CompleteEnter(UserContext& user, const UserWorldState& state);
		void								CompleteLeave(UserContext& user, const UserWorldState& state);
		
		void								RefreshWorldUserContext(const UserContext& user);
		void								SuspendWorldUser(const UserContext& user);
		void								BeginWorldResync(UserContext& user);
		void								TryCompleteWorldResync(UserContext& user);
		void								FailWorldResync(UserContext& user);
		void								ReleaseReconnectingUser(UserContext& user);
		void								CompleteDisconnectedWorldLeave(UserId userId, const WorldRef& world);

	private:

		std::atomic<uint64>						m_nextTransitionToken = 1;
		std::atomic<uint64>						m_nextWorldSyncToken  = 1;

		uint64									m_worldSyncTimeoutNs  = 15_s;
		uint64									m_reconnectTimeoutNs  = 15_s;

		ServerNetworkManager*					m_owner = nullptr;
		WorldTemplateDatabase					m_templatesDB;
		WorldArchetypeDatabase					m_archetypesDB;
		WorldInstanceDatabase					m_instancesDB;
		ActorArchetypeDatabase					m_actorArchetypesDB;
		std::unique_ptr<WorldConfigResolver>	m_configResolver;

		std::mutex								m_worldMutex;
		std::unordered_map<uint64, WorldSlot>	m_worldSlots;
	};
}
