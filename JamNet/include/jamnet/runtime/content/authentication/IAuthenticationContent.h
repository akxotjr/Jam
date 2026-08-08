#pragma once

#include "jamnet/runtime/session/UserContext.h"

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace jam::net
{
	struct PasswordCredential
	{
		std::string loginId;
		std::string password;
	};

	struct TicketCredential
	{
		std::vector<uint8> ticket;
	};

	using LoginCredential = std::variant<PasswordCredential, TicketCredential>;
	using AuthenticationCompleted = std::function<void(AccountId)>;

	class IAuthenticationContent
	{
	public:
		virtual ~IAuthenticationContent() = default;
		virtual void Authenticate(LoginCredential credential, AuthenticationCompleted completed) = 0;
	};
}
