#include "pch.h"
#include "AccountStore.h"

namespace m1
{
	bool AccountStore::Register(AccountRecord account)
	{
		if (account.accountId == jam::net::kInvalidAccountId || account.loginId.empty() || account.password.empty())
			return false;

		std::unique_lock lock(m_mutex);
		if (m_accountsById.contains(account.accountId) || m_accountIdsByLogin.contains(account.loginId))
			return false;

		const jam::net::AccountId accountId = account.accountId;
		m_accountIdsByLogin.emplace(account.loginId, accountId);
		m_accountsById.emplace(accountId, std::move(account));
		return true;
	}

	std::optional<jam::net::AccountId> AccountStore::Authenticate(std::string_view loginId, std::string_view password) const
	{
		if (loginId.empty() || password.empty())
			return std::nullopt;

		std::shared_lock lock(m_mutex);
		const auto loginIt = m_accountIdsByLogin.find(std::string(loginId));
		if (loginIt == m_accountIdsByLogin.end())
			return std::nullopt;

		const auto accountIt = m_accountsById.find(loginIt->second);
		if (accountIt == m_accountsById.end() || accountIt->second.password != password)
			return std::nullopt;
		return accountIt->first;
	}

	std::optional<AccountRecord> AccountStore::Find(jam::net::AccountId accountId) const
	{
		if (accountId == jam::net::kInvalidAccountId)
			return std::nullopt;

		std::shared_lock lock(m_mutex);
		const auto it = m_accountsById.find(accountId);
		return it != m_accountsById.end() ? std::optional<AccountRecord>{ it->second } : std::nullopt;
	}
}
