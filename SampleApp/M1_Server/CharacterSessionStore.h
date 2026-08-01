#pragma once

#include <jamnet/runtime/session/UserContext.h>
#include <jamnet/runtime/world/actor/ActorId.h>
#include <jamnet/runtime/world/lifecycle/WorldTransitionTypes.h>

#include <mutex>
#include <optional>
#include <unordered_map>

namespace m1
{
	struct PersistentCharacterState
	{
		// 영속 캐릭터: 계정과 연결되며, 개별 World의 Player actor 수명보다 오래 유지되는 콘텐츠 상태다.
		jam::net::AccountId	accountId = jam::net::kInvalidAccountId;
		uint64				characterId = 0;
	};

	struct PlayerActorBinding
	{
		jam::net::WorldRuntimeRef	world{};
		jam::net::ActorId			actorId = jam::net::ActorId::Invalid();

		bool IsValid() const noexcept { return world.IsValid() && actorId.IsValid(); }
	};

	struct CharacterSessionContext
	{
		jam::net::UserId						 userId = jam::net::kInvalidUserId;
		PersistentCharacterState				 persistentCharacter;
		std::optional<PlayerActorBinding>		 activePlayer;
		std::optional<PlayerActorBinding>		 rollbackSourcePlayer;
		std::optional<jam::net::WorldRuntimeRef> pendingTargetWorld;
		jam::net::WorldTransitionToken			 transitionToken{};
	};

	class CharacterSessionStore
	{
	public:
		bool BeginMaterialization(jam::net::AccountId accountId, jam::net::UserId userId, jam::net::WorldTransitionToken token, const jam::net::WorldRuntimeRef& target);
		bool CompleteMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token, PlayerActorBinding targetPlayer);
		void FailMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token);

		std::optional<PlayerActorBinding> RollbackMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token, const jam::net::WorldRuntimeRef& target);
		std::optional<PlayerActorBinding> CommitLeave(jam::net::UserId userId, jam::net::WorldTransitionToken token, const jam::net::WorldRuntimeRef& source);
		std::optional<PlayerActorBinding> FindActivePlayer(jam::net::UserId userId, const jam::net::WorldRuntimeRef& world) const;

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<jam::net::UserId, CharacterSessionContext> m_sessions;
	};
}
