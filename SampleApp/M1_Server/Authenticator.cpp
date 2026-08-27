#include "pch.h"
#include "Authenticator.h"
#include "AccountStore.h"

namespace m1
{
	namespace
	{
		inline constexpr uint32 kPasswordAuthScheme = 0;
	}

	Authenticator::Authenticator(std::shared_ptr<AccountStore> accounts) : m_accounts(std::move(accounts))
	{
	}

	void Authenticator::Authenticate(jam::net::AuthCredential credential, jam::net::AuthCompleted completed)
	{
		if (!completed)
			return;
		if (!m_accounts || credential.scheme != kPasswordAuthScheme || credential.field0.size > credential.field0.bytes.size() || credential.field1.size > credential.field1.bytes.size())
		{
			completed({});
			return;
		}

		const std::string_view loginId(reinterpret_cast<const char*>(credential.field0.bytes.data()), credential.field0.size);
		const std::string_view password(reinterpret_cast<const char*>(credential.field1.bytes.data()), credential.field1.size);
		const jam::net::AccountId accountId = m_accounts->Authenticate(loginId, password).value_or(jam::net::kInvalidAccountId);
		completed({ .success = accountId != jam::net::kInvalidAccountId, .retryable = false, .principalId = accountId });
	}
}
