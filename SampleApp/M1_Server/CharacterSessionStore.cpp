#include "pch.h"
#include "CharacterSessionStore.h"

namespace m1
{
	CharacterSessionStore::CharacterSessionStore(std::shared_ptr<CharacterStore> characters)
		: m_characters(std::move(characters))
	{
	}

	bool CharacterSessionStore::SelectCharacter(jam::net::AccountId accountId, jam::net::UserId userId, const CharacterRecord& character)
	{
		if (accountId == jam::net::kInvalidAccountId || userId == jam::net::kInvalidUserId || !character.IsValid() || character.accountId != accountId)
			return false;

		std::scoped_lock lock(m_mutex);
		auto [it, inserted] = m_sessions.try_emplace(userId);
		CharacterSessionContext& session = it->second;
		if (!inserted && (session.persistentCharacter.accountId != accountId || session.transitionToken.IsValid() || session.activePlayer))
			return false;

		session.userId = userId;
		session.persistentCharacter = {
			.accountId			= accountId,
			.characterId		= character.characterId,
			.name				= character.name,
			.actorArchetypeKey	= character.actorArchetypeKey,
			.worldArchetypeKey	= character.worldArchetypeKey,
			.position			= character.position,
		};
		return true;
	}

	bool CharacterSessionStore::UpdateLocation(jam::net::UserId userId, jam::net::WorldArchetypeKey worldArchetypeKey, const jam::px::Vec3& position)
	{
		if (userId == jam::net::kInvalidUserId || !jam::IsValidAssetKey(worldArchetypeKey) || !position.IsFinite())
			return false;

		CharacterId characterId = kInvalidCharacterId;
		{
			std::scoped_lock lock(m_mutex);
			const auto session = m_sessions.find(userId);
			if (session == m_sessions.end() || !session->second.persistentCharacter.IsValid())
				return false;

			session->second.persistentCharacter.worldArchetypeKey = worldArchetypeKey;
			session->second.persistentCharacter.position = position;
			characterId = session->second.persistentCharacter.characterId;
		}

		return m_characters && m_characters->UpdateLocation(characterId, worldArchetypeKey, position);
	}

	bool CharacterSessionStore::BeginMaterialization(
		jam::net::AccountId accountId,
		jam::net::UserId userId,
		jam::net::WorldTransitionToken token,
		const jam::net::WorldRef& target,
		PersistentCharacterState& outCharacter)
	{
		if (accountId == jam::net::kInvalidAccountId || userId == jam::net::kInvalidUserId
			|| !token.IsValid() || !target.IsValid())
		{
			return false;
		}

		std::scoped_lock lock(m_mutex);
		auto it = m_sessions.find(userId);
		if (it == m_sessions.end())
			return false;

		CharacterSessionContext& session = it->second;
		if (!session.persistentCharacter.IsValid() || session.persistentCharacter.accountId != accountId)
		{
			return false;
		}

		if (session.transitionToken.IsValid() && session.transitionToken.value != token.value)
			return false;
		if (session.activePlayer && session.activePlayer->world == target)
			return false;

		session.transitionToken		 = token;
		session.rollbackSourcePlayer = session.activePlayer;
		session.pendingTargetWorld   = target;

		outCharacter = session.persistentCharacter;
		return true;
	}

	bool CharacterSessionStore::CompleteMaterialization(
		jam::net::UserId userId,
		jam::net::WorldTransitionToken token,
		PlayerActorBinding targetPlayer)
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_sessions.find(userId);
		if (it == m_sessions.end() || !targetPlayer.IsValid())
			return false;

		CharacterSessionContext& session = it->second;
		if (session.transitionToken.value != token.value || !session.pendingTargetWorld
			|| *session.pendingTargetWorld != targetPlayer.world)
		{
			return false;
		}

		session.activePlayer = targetPlayer;
		session.pendingTargetWorld.reset();
		if (!session.rollbackSourcePlayer)
			session.transitionToken = {};
		return true;
	}

	void CharacterSessionStore::FailMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token)
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_sessions.find(userId);
		if (it == m_sessions.end() || it->second.transitionToken.value != token.value)
			return;

		it->second.pendingTargetWorld.reset();
		it->second.activePlayer = it->second.rollbackSourcePlayer;
		it->second.rollbackSourcePlayer.reset();
		it->second.transitionToken = {};
	}

	std::optional<PlayerActorBinding> CharacterSessionStore::RollbackMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token, const jam::net::WorldRef& targetWorld)
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_sessions.find(userId);
		if (it == m_sessions.end())
			return std::nullopt;

		CharacterSessionContext& session = it->second;
		if (session.transitionToken.value != token.value)
		{
			if (!session.transitionToken.IsValid() && session.activePlayer && session.activePlayer->world == targetWorld && !session.rollbackSourcePlayer)
			{
				const PlayerActorBinding target = *session.activePlayer;
				m_sessions.erase(it);
				return target;
			}
			return std::nullopt;
		}

		std::optional<PlayerActorBinding> target;
		if (session.activePlayer && (!session.rollbackSourcePlayer || session.activePlayer->world != session.rollbackSourcePlayer->world))
		{
			target = session.activePlayer;
		}

		session.activePlayer = session.rollbackSourcePlayer;
		session.rollbackSourcePlayer.reset();
		session.pendingTargetWorld.reset();
		session.transitionToken = {};
		return target;
	}

	std::optional<PlayerActorBinding> CharacterSessionStore::CommitLeave(jam::net::UserId userId, jam::net::WorldTransitionToken token, const jam::net::WorldRef& source)
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_sessions.find(userId);
		if (it == m_sessions.end())
			return std::nullopt;

		CharacterSessionContext& session = it->second;
		if (session.rollbackSourcePlayer && session.rollbackSourcePlayer->world == source)
		{
			const PlayerActorBinding result = *session.rollbackSourcePlayer;
			session.rollbackSourcePlayer.reset();
			session.transitionToken = {};
			return result;
		}
		if (session.activePlayer && session.activePlayer->world == source)
		{
			const PlayerActorBinding result = *session.activePlayer;
			m_sessions.erase(it);
			return result;
		}

		(void)token;
		return std::nullopt;
	}

	std::optional<PlayerActorBinding> CharacterSessionStore::FindActivePlayer(jam::net::UserId userId, const jam::net::WorldRef& world) const
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_sessions.find(userId);
		if (it == m_sessions.end() || !it->second.activePlayer || it->second.activePlayer->world != world)
			return std::nullopt;

		return it->second.activePlayer;
	}

	std::optional<PersistentCharacterState> CharacterSessionStore::FindSelectedCharacter(jam::net::UserId userId) const
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_sessions.find(userId);
		if (it == m_sessions.end() || !it->second.persistentCharacter.IsValid())
			return std::nullopt;

		return it->second.persistentCharacter;
	}
}
