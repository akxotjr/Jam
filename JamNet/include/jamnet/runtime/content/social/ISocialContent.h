#pragma once

#include "jamnet/runtime/content/social/SocialTypes.h"
#include "jamnet/runtime/content/social/ISocialDelivery.h"
#include "jamnet/runtime/session/UserContext.h"

namespace jam::net
{
	class ISocialContent
	{
	public:
		virtual ~ISocialContent() = default;

		// delivery is scoped to this call and always stamps sender.userId on emitted messages.
		virtual void HandleCommand(const SocialPrincipal& sender, const SocialCommand& command, ISocialDelivery& delivery) = 0;
		virtual void OnUserConnected(const SocialPrincipal& principal) {}
		virtual void OnUserDisconnected(UserId userId) {}
	};
}
