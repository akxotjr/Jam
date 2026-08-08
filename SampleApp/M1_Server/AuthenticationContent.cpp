#include "pch.h"
#include "AuthenticationContent.h"
#include "AccountStore.h"

namespace m1
{
	AuthenticationContent::AuthenticationContent(std::shared_ptr<AccountStore> accounts)
		: m_accounts(std::move(accounts))
	{
	}

	void AuthenticationContent::Authenticate(jam::net::LoginCredential credential, jam::net::AuthenticationCompleted completed)
	{
		if (!completed)
			return;

		const auto* password = std::get_if<jam::net::PasswordCredential>(&credential);
		if (!m_accounts || !password)
		{
			completed(jam::net::kInvalidAccountId);
			return;
		}

		completed(m_accounts->Authenticate(password->loginId, password->password)
			.value_or(jam::net::kInvalidAccountId));
	}
}
