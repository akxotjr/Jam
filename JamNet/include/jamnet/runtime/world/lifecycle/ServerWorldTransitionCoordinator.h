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
		
		void					Enter(UserId userId, const EnterWorldRequest& request, Timestamp nowNs);
		void					Leave(UserId userId, const LeaveWorldRequest& request);
		
		void					Tick(Timestamp now);
		
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
			eWorldTransitionFailure		failure = eWorldTransitionFailure::None;
		};
		struct TransitionTimedOutEvent
		{
			WorldTransitionContinuation	continuation;
			Timestamp					now	   = Timestamp::Zero();
		};
		struct UserDisconnectedEvent
		{
			WorldTransitionContinuation continuation;
		};

		using WorldTransitionEvent = std::variant<
			WorldReadyEvent, 
			WorldCommandCompletedEvent, 
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

		void								EnterMembershipOnUserShard(UserId userId, const EnterWorldRequest& request, Timestamp now);
		void								LeaveMembershipOnUserShard(UserId userId, const LeaveWorldRequest& request);
		bool								AdvanceMembershipTransition(UserContext& user, eWorldTransitionPhase phase);
		void								FailMembershipTransition(UserContext& user, eWorldTransitionFailure failure);
		void								HandleWorldTransitionEvent(const WorldTransitionEvent& event);
		bool								SubmitMembershipCommand(const WorldRef& world, const WorldTransitionContinuation& continuation, std::function<eWorldTransitionFailure(WorldShardState&)> command);
		void								FinishMembershipFailure(UserContext& user);
		void								CompleteMembershipTransition(UserContext& user);
		void								ReleaseMembershipUser(UserContext& user);
		
		WorldTransitionToken				IssueTransitionToken();
		
		std::optional<WorldInstanceRef>		ResolveDestination(const EnterWorldRequest& request) const;
		
		
		void								EnsureWorldAsync(const WorldInstanceRef& instance, WorldReadyFn completed);
		void								StartWorldCreation(const WorldInstanceRef& instance);
		void								CompleteWorldCreation(const WorldInstanceRef& instance, std::optional<WorldRecord> record);
		void								CompleteWorldDestruction(const WorldRef& world, bool destroyed);
		
		void								CompleteEnter(UserContext& user, const UserWorldState& state);
		void								CompleteLeave(UserContext& user, const UserWorldState& state);
		
		void								RefreshWorldUserContext(const UserContext& user);
		void								SuspendWorldUser(const UserContext& user);

	private:

		std::atomic<uint64>						m_nextTransitionToken = 1;

		Duration								m_transitionTimeout = 15_s;
		Duration								m_reconnectTimeout  = 15_s;

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
