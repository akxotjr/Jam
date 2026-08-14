#pragma once

#include "CharacterStore.h"

#include <jamnet/runtime/session/UserContext.h>
#include <jamnet/runtime/world/actor/ActorId.h>
#include <jamnet/runtime/world/lifecycle/WorldTransitionTypes.h>

#include <mutex>
#include <memory>
#include <optional>
#include <unordered_map>

namespace m1
{
	struct PersistentCharacterState
	{
		// 영속 캐릭터: 계정과 연결되며, 개별 World의 Player actor 수명보다 오래 유지되는 콘텐츠 상태다.
		jam::net::AccountId			accountId   = jam::net::kInvalidAccountId;
		CharacterId					characterId = kInvalidCharacterId;
		std::string					name;
		jam::net::ActorArchetypeKey	actorArchetypeKey{};
		jam::net::WorldArchetypeKey	worldArchetypeKey{};
		jam::px::Vec3				position{};

		bool IsValid() const noexcept
		{
			return accountId != jam::net::kInvalidAccountId
				&& characterId != kInvalidCharacterId
				&& !name.empty()
				&& static_cast<bool>(actorArchetypeKey)
				&& static_cast<bool>(worldArchetypeKey)
				&& position.IsFinite();
		}
	};

	struct PlayerActorBinding
	{
		jam::net::WorldRef	world   = {};
		jam::net::ActorId	actorId = jam::net::ActorId::Invalid();

		bool IsValid() const noexcept { return world.IsValid() && actorId.IsValid(); }
	};

	struct CharacterSessionContext
	{
		jam::net::UserId						userId = jam::net::kInvalidUserId;
		PersistentCharacterState				persistentCharacter;
		std::optional<PlayerActorBinding>		activePlayer;
		std::optional<PlayerActorBinding>		rollbackSourcePlayer;
		std::optional<jam::net::WorldRef>		pendingTargetWorld;
		jam::net::WorldTransitionToken			transitionToken{};
	};

	class CharacterSessionStore
	{
	public:
		explicit CharacterSessionStore(std::shared_ptr<CharacterStore> characters);

		bool SelectCharacter(jam::net::AccountId accountId, jam::net::UserId userId, const CharacterRecord& character);
		bool UpdateLocation(jam::net::UserId userId, jam::net::WorldArchetypeKey worldArchetypeKey, const jam::px::Vec3& position);
		bool BeginMaterialization(jam::net::AccountId accountId, jam::net::UserId userId, jam::net::WorldTransitionToken token, const jam::net::WorldRef& target, PersistentCharacterState& outCharacter);
		bool CompleteMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token, PlayerActorBinding targetPlayer);
		void FailMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token);

		std::optional<PlayerActorBinding>		RollbackMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token, const jam::net::WorldRef& target);
		std::optional<PlayerActorBinding>		CommitLeave(jam::net::UserId userId, jam::net::WorldTransitionToken token, const jam::net::WorldRef& source);
		std::optional<PlayerActorBinding>		FindActivePlayer(jam::net::UserId userId, const jam::net::WorldRef& world) const;
		std::optional<PersistentCharacterState> FindSelectedCharacter(jam::net::UserId userId) const;

	private:
		mutable std::mutex m_mutex;
		std::shared_ptr<CharacterStore> m_characters;
		std::unordered_map<jam::net::UserId, CharacterSessionContext> m_sessions;
	};
}
