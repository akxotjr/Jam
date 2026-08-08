#pragma once

#include <jamnet/runtime/content/authentication/IAuthenticationContent.h>

#include <memory>

namespace m1
{
	class AccountStore;

	class AuthenticationContent final : public jam::net::IAuthenticationContent
	{
	public:
		explicit AuthenticationContent(std::shared_ptr<AccountStore> accounts);
		void Authenticate(jam::net::LoginCredential credential, jam::net::AuthenticationCompleted completed) override;

	private:
		std::shared_ptr<AccountStore> m_accounts;
	};
}
