#pragma once

#include "jamnet/runtime/social/SocialTypes.h"
#include "jamnet/runtime/social/ISocialDelivery.h"
#include "jamnet/runtime/session/UserContext.h"

namespace jam::net
{
	class IServerSocialContent
	{
	public:
		virtual ~IServerSocialContent() = default;

		// delivery is scoped to this call and always stamps sender.userId on emitted messages.
		virtual void HandleCommand(const SocialPrincipal& sender, const SocialCommand& command, ISocialDelivery& delivery) = 0;
		virtual void OnUserDisconnected(UserId userId) {}
	};
}
