#pragma once

#include "jamnet/core/net/Buffer.h"
#include "jamnet/runtime/content/social/SocialTypes.h"

namespace jam::net::codec
{
	Packet MakeSocialCommandPacket(const SocialCommand& command);
	Packet MakeSocialMessagePacket(const SocialMessage& message);

	bool DecodeSocialCommand(const void* payload, size_t payloadSize, SocialCommand& out);
	bool DecodeSocialMessage(const void* payload, size_t payloadSize, SocialMessage& out);
}
