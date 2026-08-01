#pragma once

#include <jambase/JamTypes.h>

#include "jamnet/runtime/session/ClientRequestId.h"
#include "jamnet/runtime/session/UserContext.h"

#include <cstddef>
#include <vector>


namespace jam::net
{
	enum class eSocialAudience : uint8
	{
		Direct,
		Group,
		Global,
	};

	struct SocialAddress
	{
		eSocialAudience			audience = eSocialAudience::Direct;		// The audience of the message
		uint64					scopeId  = 0;							// The scope of the message, e.g., userId for Direct, worldId for World, etc. 
	};

	struct SocialCommand
	{
		ClientRequestId			requestId	= kInvalidClientRequestId;
		SocialAddress			destination	= {};
		uint16					contentType	= 0;	// The type of the payload, e.g., text, emoji, etc. 
		std::vector<std::byte>	payload;
	};

	struct SocialMessage
	{
		uint64					messageId	= 0;
		UserId					sender		= kInvalidUserId;	// filled in by the server from authenticated session
		SocialAddress			destination	= {};
		uint16					contentType	= 0;
		std::vector<std::byte>	payload;
	};

	struct SocialPrincipal
	{
		AccountId				accountId	= kInvalidAccountId;
		UserId					userId		= kInvalidUserId;
		UserPhysicalWorldState	world		= {};
	};



}
