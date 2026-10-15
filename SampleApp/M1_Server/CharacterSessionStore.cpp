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

	bool CharacterSessionStore::BeginMembershipMaterialization(
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

namespace m1
{

	bool CharacterSessionStore::CompleteMembershipMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token, PlayerActorBinding player)
	{
		std::scoped_lock lock(m_mutex);
		auto it = m_sessions.find(userId);
		if (it == m_sessions.end() || !player.IsValid()) return false;
		auto& session = it->second;
		if (session.transitionToken != token || !session.pendingTargetWorld || *session.pendingTargetWorld != player.world)
			return false;
		session.activePlayer = player;
		session.pendingTargetWorld.reset();
		// Keep the token even on first entry until the client accepts or cancels.
		return true;
	}

	bool CharacterSessionStore::CancelMembershipMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token, const jam::net::WorldRef& target)
	{
		std::scoped_lock lock(m_mutex);
		auto it = m_sessions.find(userId);
		if (it == m_sessions.end()) return true; // Preparation never acquired character state.
		auto& session = it->second;
		if (session.transitionToken != token)
			return !session.transitionToken.IsValid() && (!session.activePlayer || session.activePlayer->world != target);
		session.activePlayer = session.rollbackSourcePlayer;
		session.rollbackSourcePlayer.reset();
		session.pendingTargetWorld.reset();
		session.transitionToken = {};
		return true;
	}

	bool CharacterSessionStore::ResetMembershipActivation(jam::net::UserId userId, jam::net::WorldTransitionToken token, const jam::net::WorldRef& target)
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_sessions.find(userId);
		if (it == m_sessions.end() || it->second.transitionToken != token) return false;
		auto& session = it->second;
		session.activePlayer.reset();
		session.pendingTargetWorld = target;
		return true;
	}

	bool CharacterSessionStore::FinalizeMembershipSource(jam::net::UserId userId, jam::net::WorldTransitionToken token, const PlayerActorBinding& source)
	{
		std::scoped_lock lock(m_mutex);
		auto it = m_sessions.find(userId);
		if (it == m_sessions.end()) return false;
		auto& session = it->second;
		// A committed Member may disconnect before its first activation completes.
		if (!session.rollbackSourcePlayer && (session.pendingTargetWorld == source.world
			|| (session.activePlayer && session.activePlayer->world == source.world && session.activePlayer->actorId == source.actorId)))
		{
			m_sessions.erase(it);
			return true;
		}
		if (session.transitionToken.IsValid() && session.transitionToken != token) return false;
		if (session.rollbackSourcePlayer && session.rollbackSourcePlayer->world == source.world
			&& session.rollbackSourcePlayer->actorId == source.actorId)
		{
			if (session.activePlayer && session.activePlayer->world == source.world)
				session.activePlayer.reset();
			session.rollbackSourcePlayer.reset();
			if (!session.pendingTargetWorld) session.transitionToken = {};
			return true;
		}
		if (session.activePlayer && session.activePlayer->world == source.world && session.activePlayer->actorId == source.actorId)
		{
			m_sessions.erase(it);
			return true;
		}
		return false;
	}

	void CharacterSessionStore::AcceptMembershipMaterialization(jam::net::UserId userId, jam::net::WorldTransitionToken token)
	{
		std::scoped_lock lock(m_mutex);
		auto it = m_sessions.find(userId);
		if (it != m_sessions.end() && it->second.transitionToken == token
			&& !it->second.rollbackSourcePlayer && !it->second.pendingTargetWorld)
			it->second.transitionToken = {};
	}
}
