#pragma once

#include <jambase/JamTypes.h>

#include <array>

namespace jam::net
{
	template<size_t Capacity>
	struct AuthField
	{
		uint16						size  = 0;
		std::array<uint8, Capacity> bytes = {};
	};

	struct AuthCredential
	{
		uint32				scheme = 0;

		AuthField<256>		field0;
		AuthField<256>		field1;
	};

	struct AuthResult
	{
		bool				success		= false;
		bool				retryable	= false;
		uint64				principalId = 0;
		uint32				errorCode	= 0;
	};

	using AuthCompleted = std::function<void(AuthResult)>;

	class IAuthenticator
	{
	public:
		virtual ~IAuthenticator() = default;
		virtual void Authenticate(AuthCredential credential, AuthCompleted completed) = 0;
	};
}
