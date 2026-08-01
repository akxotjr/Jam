#pragma once

#include "jamnet/runtime/social/SocialTypes.h"

namespace jam::net
{
	class ISocialDelivery
	{
	public:
		virtual ~ISocialDelivery() = default;

		virtual void SendTo(UserId userId, const SocialMessage& message) = 0;
		virtual void SendToWorld(const WorldRuntimeRef& world, const SocialMessage& message) = 0;
		virtual void Broadcast(const SocialMessage& message) = 0;
	};
}
