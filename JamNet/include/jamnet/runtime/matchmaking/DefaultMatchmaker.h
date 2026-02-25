#pragma once
#include "IMatchmaker.h"


namespace jam::net
{
	class DefaultMatchmaker : public IMatchmaker
	{
	public:
		DefaultMatchmaker() = default;
		~DefaultMatchmaker() override = default;

		void					Init(ServerNetworkManager* manager) override;
		MatchmakeResult			RequestGroupId(const MatchmakeRequest& req) override;

	private:
		ServerNetworkManager*	m_netManager = nullptr;
	};
}

