#pragma once

#include "jamnet/runtime/content/social/SocialTypes.h"

namespace jam::net
{
	class ISocialDelivery
	{
	public:
		virtual ~ISocialDelivery() = default;

		virtual void SendTo(UserId userId, const SocialMessage& message) = 0;
		virtual void SendToWorld(const WorldRef& world, const SocialMessage& message) = 0;
		virtual void Broadcast(const SocialMessage& message) = 0;
	};
}
