#pragma once

#include "jamnet/core/net/Buffer.h"
#include "jamnet/runtime/content/generic/GenericContentTypes.h"

namespace jam::net::codec
{
	Packet MakeContentRequestPacket(const GenericContentRequest& request);
	Packet MakeContentResponsePacket(const GenericContentResponse& response);

	bool DecodeContentRequest(const void* payload, size_t payloadSize, GenericContentRequest& out);
	bool DecodeContentResponse(const void* payload, size_t payloadSize, GenericContentResponse& out);
}
