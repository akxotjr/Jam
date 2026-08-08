#pragma once

#include "CharacterSessionStore.h"
#include "CharacterStore.h"

#include <jamnet/runtime/content/social/ISocialContent.h>

#include <optional>
#include <unordered_map>

namespace m1
{
	inline constexpr uint16 kTextChatContentType = 1;
	inline constexpr size_t kMaxChatTextBytes = 512;
	inline constexpr size_t kMaxChatPayloadBytes = 1024;

	class SocialContent final : public jam::net::ISocialContent
	{
	public:
		SocialContent(std::shared_ptr<CharacterStore> characters,
			std::shared_ptr<CharacterSessionStore> sessions);

		void HandleCommand(const jam::net::SocialPrincipal& sender, const jam::net::SocialCommand& command, jam::net::ISocialDelivery& delivery) override;
		void OnUserConnected(const jam::net::SocialPrincipal& principal) override;
		void OnUserDisconnected(jam::net::UserId userId) override;

	private:
		std::optional<jam::net::UserId> ResolveRecipientUser(const jam::net::SocialRecipient& recipient) const;

	private:
		uint64	m_nextMessageId = 1;
		std::unordered_map<jam::net::AccountId, jam::net::UserId> m_usersByAccount;
		std::unordered_map<jam::net::UserId, jam::net::AccountId> m_accountsByUser;
		std::shared_ptr<CharacterStore> m_characters;
		std::shared_ptr<CharacterSessionStore> m_sessions;
	};
}
