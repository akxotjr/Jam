#pragma once

#include "CharacterSessionStore.h"
#include "CharacterStore.h"

#include <jamnet/runtime/content/generic/IGenericContent.h>

#include <memory>

namespace m1
{
	inline constexpr jam::net::GenericContentOperationCode kCharacterListOperation = 1;
	inline constexpr jam::net::GenericContentOperationCode kCharacterSelectOperation = 2;

	class CharacterContent final : public jam::net::IGenericContent
	{
	public:
		CharacterContent(std::shared_ptr<CharacterStore> characters, std::shared_ptr<CharacterSessionStore> sessions);

		void HandleRequest(const jam::net::GenericContentPrincipal& principal, const jam::net::GenericContentRequest& request, jam::net::GenericContentRequestCompleted completed) override;

	private:
		jam::net::GenericContentResponse HandleList(const jam::net::GenericContentPrincipal& principal, const jam::net::GenericContentRequest& request) const;
		jam::net::GenericContentResponse HandleSelect(const jam::net::GenericContentPrincipal& principal, const jam::net::GenericContentRequest& request) const;

	private:
		std::shared_ptr<CharacterStore>			m_characters;
		std::shared_ptr<CharacterSessionStore>	m_sessions;
	};
}
