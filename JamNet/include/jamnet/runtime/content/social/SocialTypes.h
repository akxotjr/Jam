#pragma once

#include <jambase/JamTypes.h>

#include "jamnet/runtime/session/ClientRequestId.h"
#include "jamnet/runtime/session/UserContext.h"

#include <cstddef>
#include <string>
#include <vector>


namespace jam::net
{
	enum class eSocialAudience : uint8
	{
		Direct,
		Group,
		Global,
	};

	enum class eSocialRecipientKind : uint8
	{
		None,
		AccountId,
		CharacterId,
		CharacterName,
	};

	struct SocialRecipient
	{
		eSocialRecipientKind	kind = eSocialRecipientKind::None;
		uint64					id	 = 0;
		std::string				name;
	};

	struct SocialAddress
	{
		eSocialAudience			audience = eSocialAudience::Direct;		// The audience of the message
		uint64					scopeId  = 0;
		SocialRecipient			recipient = {};
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
		UserWorldState			world		= {};
	};



}
