#pragma once

#include <jamnet/core/net/IAuthenticator.h>

#include <memory>

namespace m1
{
	class AccountStore;

	class Authenticator final : public jam::net::IAuthenticator
	{
	public:
		explicit Authenticator(std::shared_ptr<AccountStore> accounts);
		void Authenticate(jam::net::AuthCredential credential, jam::net::AuthCompleted completed) override;

	private:
		std::shared_ptr<AccountStore> m_accounts;
	};
}
