#include "pch.h"
#include "SocialContent.h"

namespace m1
{
	namespace
	{
		bool IsValidUtf8(const std::vector<std::byte>& payload)
		{
			const auto* bytes = reinterpret_cast<const uint8*>(payload.data());
			for (size_t i = 0; i < payload.size();)
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

				if (i + continuationCount > payload.size())
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
	}

	void SocialContent::HandleCommand(const jam::net::SocialPrincipal& sender, const jam::net::SocialCommand& command, jam::net::ISocialDelivery& delivery)
	{
		if (sender.userId == jam::net::kInvalidUserId
			|| command.contentType != kTextChatContentType
			|| command.payload.empty()
			|| command.payload.size() > kMaxChatTextBytes
			|| !IsValidUtf8(command.payload))
			return;

		jam::net::SocialMessage message{
			.messageId	 = m_nextMessageId++,
			.destination = command.destination,
			.contentType = command.contentType,
			.payload	 = command.payload,
		};

		switch (command.destination.audience)
		{
		case jam::net::eSocialAudience::Direct:
		{
			const auto target = static_cast<jam::net::UserId>(command.destination.scopeId);
			if (target == jam::net::kInvalidUserId)
				return;

			delivery.SendTo(target, message);
			if (target != sender.userId)
				delivery.SendTo(sender.userId, message);
			return;
		}

		case jam::net::eSocialAudience::Group:
			if (!sender.world.main || sender.world.main->worldId != command.destination.scopeId)
				return;
			delivery.SendToWorld(*sender.world.main, message);
			return;

		case jam::net::eSocialAudience::Global:
			delivery.Broadcast(message);
			return;
		}
	}
}
