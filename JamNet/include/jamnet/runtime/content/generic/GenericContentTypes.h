#pragma once

#include <jambase/JamTypes.h>

#include "jamnet/runtime/session/ClientRequestId.h"
#include "jamnet/runtime/session/UserContext.h"

#include <cstddef>
#include <vector>

namespace jam::net
{
	using GenericContentOperationCode = uint64;
	inline constexpr GenericContentOperationCode kInvalidGenericContentOpCode  = 0;
	inline constexpr size_t						 kMaxGenericContentPayloadBytes = 1024;

	enum class eGenericContentResponseStatus : uint8
	{
		None,
		Succeeded,
		Rejected,
		InvalidRequest,
		Unavailable,
		InternalError,
	};

	struct GenericContentRequest
	{
		ClientRequestId				requestId	= kInvalidClientRequestId;
		GenericContentOperationCode	opCode		= kInvalidGenericContentOpCode;
		std::vector<std::byte>		payload;

		bool IsValid() const
		{
			return requestId != kInvalidClientRequestId && opCode != kInvalidGenericContentOpCode && payload.size() <= kMaxGenericContentPayloadBytes;
		}
	};

	struct GenericContentResponse
	{
		ClientRequestId					requestId	= kInvalidClientRequestId;
		GenericContentOperationCode		opCode		= kInvalidGenericContentOpCode;
		eGenericContentResponseStatus	status		= eGenericContentResponseStatus::None;
		uint32							resultCode	= 0;
		std::vector<std::byte>			payload;
	};

	struct GenericContentPrincipal
	{
		AccountId accountId = kInvalidAccountId;
		UserId	  userId	= kInvalidUserId;
	};
}
