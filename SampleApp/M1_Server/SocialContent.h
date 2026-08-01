#pragma once

#include <jamnet/runtime/social/IServerSocialContent.h>

#include <cstddef>

namespace m1
{
	inline constexpr uint16 kTextChatContentType = 1;
	inline constexpr size_t kMaxChatTextBytes = 512;

	class SocialContent final : public jam::net::IServerSocialContent
	{
	public:
		void HandleCommand(
			const jam::net::SocialPrincipal& sender,
			const jam::net::SocialCommand& command,
			jam::net::ISocialDelivery& delivery) override;

	private:
		uint64	m_nextMessageId = 1;
	};
}
