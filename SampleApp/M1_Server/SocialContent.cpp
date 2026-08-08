#include "pch.h"
#include "SocialContent.h"

#include <Protocol/Cpp/chat_content_generated.h>

namespace m1
{
	namespace
	{
		bool IsValidUtf8(const uint8* bytes, size_t size)
		{
			for (size_t i = 0; i < size;)
			{
				const uint8 lead = bytes[i++];
				if (lead == 0)
					return false;
				if (lead <= 0x7F)
					continue;

				uint32 codePoint = 0;
				size_t continuationCount = 0;
				uint32 minimum = 0;
				if ((lead & 0xE0) == 0xC0)
				{
					codePoint = lead & 0x1F;
					continuationCount = 1;
					minimum = 0x80;
				}
				else if ((lead & 0xF0) == 0xE0)
				{
					codePoint = lead & 0x0F;
					continuationCount = 2;
					minimum = 0x800;
				}
				else if ((lead & 0xF8) == 0xF0)
				{
					codePoint = lead & 0x07;
					continuationCount = 3;
					minimum = 0x10000;
				}
				else
				{
					return false;
				}

				if (i + continuationCount > size)
					return false;
				for (size_t j = 0; j < continuationCount; ++j)
				{
					const uint8 continuation = bytes[i++];
					if ((continuation & 0xC0) != 0x80)
						return false;
					codePoint = (codePoint << 6) | (continuation & 0x3F);
				}
				if (codePoint < minimum || codePoint > 0x10FFFF
					|| (codePoint >= 0xD800 && codePoint <= 0xDFFF))
					return false;
			}
			return true;
		}

		bool DecodeChatRequest(const std::vector<std::byte>& payload, std::string_view& outText)
		{
			if (payload.empty() || payload.size() > kMaxChatPayloadBytes)
				return false;

			flatbuffers::Verifier verifier(
				reinterpret_cast<const uint8*>(payload.data()), payload.size());
			if (!verifier.VerifyBuffer<fb::fbChatText>(nullptr))
				return false;

			const auto* wire = flatbuffers::GetRoot<fb::fbChatText>(payload.data());
			const auto* text = wire->text();
			const auto* senderName = wire->sender_character_name();
			if (!text || text->size() == 0 || text->size() > kMaxChatTextBytes
				|| (senderName && !senderName->empty())
				|| !IsValidUtf8(text->Data(), text->size()))
				return false;

			outText = std::string_view(text->c_str(), text->size());
			return true;
		}

		std::vector<std::byte> MakeChatPayload(
			std::string_view text, std::string_view senderCharacterName)
		{
			flatbuffers::FlatBufferBuilder builder(1024);
			const auto root = fb::CreatefbChatText(
				builder,
				builder.CreateString(text),
				builder.CreateString(senderCharacterName));
			builder.Finish(root);
			const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
			return { begin, begin + builder.GetSize() };
		}
	}

	SocialContent::SocialContent(
		std::shared_ptr<CharacterStore> characters,
		std::shared_ptr<CharacterSessionStore> sessions)
		: m_characters(std::move(characters))
		, m_sessions(std::move(sessions))
	{
	}

	std::optional<jam::net::UserId> SocialContent::ResolveRecipientUser(
		const jam::net::SocialRecipient& recipient) const
	{
		switch (recipient.kind)
		{
		case jam::net::eSocialRecipientKind::CharacterId:
		{
			if (!m_characters || recipient.id == kInvalidCharacterId || !recipient.name.empty())
				return std::nullopt;
			const auto character = m_characters->FindById(static_cast<CharacterId>(recipient.id));
			if (!character)
				return std::nullopt;
			const auto user = m_usersByAccount.find(character->accountId);
			return user == m_usersByAccount.end() ? std::nullopt : std::optional<jam::net::UserId>(user->second);
		}
		case jam::net::eSocialRecipientKind::CharacterName:
		{
			if (!m_characters || recipient.id != 0 || recipient.name.empty())
				return std::nullopt;
			const auto character = m_characters->FindByName(recipient.name);
			if (!character)
				return std::nullopt;
			const auto user = m_usersByAccount.find(character->accountId);
			return user == m_usersByAccount.end() ? std::nullopt : std::optional<jam::net::UserId>(user->second);
		}
		case jam::net::eSocialRecipientKind::AccountId:
		case jam::net::eSocialRecipientKind::None:
		default:
			return std::nullopt;
		}
	}

	void SocialContent::OnUserConnected(const jam::net::SocialPrincipal& principal)
	{
		if (principal.accountId == jam::net::kInvalidAccountId || principal.userId == jam::net::kInvalidUserId)
			return;

		if (const auto accountIt = m_usersByAccount.find(principal.accountId);
			accountIt != m_usersByAccount.end() && accountIt->second != principal.userId)
			m_accountsByUser.erase(accountIt->second);

		if (const auto userIt = m_accountsByUser.find(principal.userId);
			userIt != m_accountsByUser.end() && userIt->second != principal.accountId)
			m_usersByAccount.erase(userIt->second);

		m_usersByAccount[principal.accountId] = principal.userId;
		m_accountsByUser[principal.userId] = principal.accountId;
	}

	void SocialContent::OnUserDisconnected(jam::net::UserId userId)
	{
		const auto it = m_accountsByUser.find(userId);
		if (it == m_accountsByUser.end())
			return;

		const auto accountIt = m_usersByAccount.find(it->second);
		if (accountIt != m_usersByAccount.end() && accountIt->second == userId)
			m_usersByAccount.erase(accountIt);
		m_accountsByUser.erase(it);
	}

	void SocialContent::HandleCommand(const jam::net::SocialPrincipal& sender, const jam::net::SocialCommand& command, jam::net::ISocialDelivery& delivery)
	{
		const auto senderCharacter = m_sessions
			? m_sessions->FindSelectedCharacter(sender.userId)
			: std::nullopt;
		std::string_view text;
		if (sender.userId == jam::net::kInvalidUserId
			|| !senderCharacter
			|| command.contentType != kTextChatContentType
			|| !DecodeChatRequest(command.payload, text))
			return;

		jam::net::SocialMessage message{
			.messageId	 = m_nextMessageId++,
			.destination = command.destination,
			.contentType = command.contentType,
			.payload	 = MakeChatPayload(text, senderCharacter->name),
		};

		switch (command.destination.audience)
		{
		case jam::net::eSocialAudience::Direct:
		{
			if (command.destination.scopeId != 0)
				return;

			const auto target = ResolveRecipientUser(command.destination.recipient);
			if (!target)
				return;

			jam::net::SocialMessage recipientMessage = message;
			recipientMessage.destination.recipient = {
				.kind = jam::net::eSocialRecipientKind::CharacterName,
				.name = senderCharacter->name,
			};

			delivery.SendTo(*target, recipientMessage);
			if (*target != sender.userId)
				delivery.SendTo(sender.userId, message);
			return;
		}

		case jam::net::eSocialAudience::Group:
			if (command.destination.recipient.kind != jam::net::eSocialRecipientKind::None || !sender.world.main || sender.world.main->worldId != command.destination.scopeId)
				return;
			delivery.SendToWorld(*sender.world.main, message);
			return;

		case jam::net::eSocialAudience::Global:
			if (command.destination.scopeId != 0 || command.destination.recipient.kind != jam::net::eSocialRecipientKind::None)
				return;
			delivery.Broadcast(message);
			return;
		}
	}
}
