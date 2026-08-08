#pragma once

#include <jamnet/runtime/session/UserContext.h>

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace m1
{
	struct AccountRecord
	{
		jam::net::AccountId accountId = jam::net::kInvalidAccountId;
		std::string			loginId;
		std::string			password;
	};

	class AccountStore
	{
	public:
		bool								Register(AccountRecord account);
		std::optional<jam::net::AccountId>	Authenticate(std::string_view loginId, std::string_view password) const;
		std::optional<AccountRecord>		Find(jam::net::AccountId accountId) const;

	private:
		mutable std::shared_mutex m_mutex;

		std::unordered_map<jam::net::AccountId, AccountRecord> m_accountsById;
		std::unordered_map<std::string, jam::net::AccountId>   m_accountIdsByLogin;
	};
}
