#pragma once

#include <jamnet/runtime/session/UserContext.h>
#include <jamnet/runtime/world/actor/ActorArchetypeDatabase.h>

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace m1
{
	using CharacterId = uint64;
	inline constexpr CharacterId kInvalidCharacterId = 0;

	struct CharacterRecord
	{
		CharacterId					characterId = kInvalidCharacterId;
		jam::net::AccountId			accountId = jam::net::kInvalidAccountId;
		std::string					name;
		jam::net::ActorArchetypeKey	actorArchetypeKey{};

		bool IsValid() const noexcept
		{
			return characterId != kInvalidCharacterId
				&& accountId != jam::net::kInvalidAccountId
				&& !name.empty()
				&& static_cast<bool>(actorArchetypeKey);
		}
	};

	class CharacterStore
	{
	public:
		bool							Register(CharacterRecord character);
		std::vector<CharacterRecord>	FindByAccount(jam::net::AccountId accountId) const;
		std::optional<CharacterRecord>	FindOwnedCharacter(jam::net::AccountId accountId, CharacterId characterId) const;
		std::optional<CharacterRecord>	FindById(CharacterId characterId) const;
		std::optional<CharacterRecord>	FindByName(std::string_view name) const;

	private:
		mutable std::shared_mutex m_mutex;

		std::unordered_map<CharacterId, CharacterRecord>					m_charactersById;
		std::unordered_map<jam::net::AccountId, std::vector<CharacterId>>	m_characterIdsByAccount;
		std::unordered_map<std::string, CharacterId>						m_characterIdsByName;
	};
}
