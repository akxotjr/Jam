#include "pch.h"
#include "CharacterStore.h"

namespace m1
{
	bool CharacterStore::Register(CharacterRecord character)
	{
		if (!character.IsValid())
			return false;

		std::unique_lock lock(m_mutex);
		const CharacterId characterId = character.characterId;
		const jam::net::AccountId accountId = character.accountId;
		const std::string characterName = character.name;
		if (m_characterIdsByName.contains(characterName))
			return false;
		if (!m_charactersById.emplace(characterId, std::move(character)).second)
			return false;

		m_characterIdsByAccount[accountId].push_back(characterId);
		m_characterIdsByName.emplace(characterName, characterId);
		return true;
	}

	std::vector<CharacterRecord> CharacterStore::FindByAccount(jam::net::AccountId accountId) const
	{
		std::shared_lock lock(m_mutex);
		std::vector<CharacterRecord> result;
		const auto ids = m_characterIdsByAccount.find(accountId);
		if (ids == m_characterIdsByAccount.end())
			return result;

		result.reserve(ids->second.size());
		for (const CharacterId characterId : ids->second)
		{
			if (const auto character = m_charactersById.find(characterId);
				character != m_charactersById.end())
				result.push_back(character->second);
		}
		return result;
	}

	std::optional<CharacterRecord> CharacterStore::FindOwnedCharacter(
		jam::net::AccountId accountId, CharacterId characterId) const
	{
		std::shared_lock lock(m_mutex);
		const auto character = m_charactersById.find(characterId);
		if (character == m_charactersById.end() || character->second.accountId != accountId)
			return std::nullopt;
		return character->second;
	}

	std::optional<CharacterRecord> CharacterStore::FindById(CharacterId characterId) const
	{
		std::shared_lock lock(m_mutex);
		const auto character = m_charactersById.find(characterId);
		if (character == m_charactersById.end())
			return std::nullopt;
		return character->second;
	}

	std::optional<CharacterRecord> CharacterStore::FindByName(std::string_view name) const
	{
		std::shared_lock lock(m_mutex);
		const auto id = m_characterIdsByName.find(std::string(name));
		if (id == m_characterIdsByName.end())
			return std::nullopt;
		const auto character = m_charactersById.find(id->second);
		if (character == m_charactersById.end())
			return std::nullopt;
		return character->second;
	}
}
