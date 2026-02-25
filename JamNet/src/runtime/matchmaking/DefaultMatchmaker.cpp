#include "pch.h"
#include "jamnet/runtime/matchmaking/DefaultMatchmaker.h"


namespace jam::net
{
	void DefaultMatchmaker::Init(ServerNetworkManager* manager)
	{
		m_netManager = manager;
	}

	MatchmakeResult DefaultMatchmaker::RequestGroupId(const MatchmakeRequest& req)
	{
		if (!m_netManager || req.principalId == 0)
			return { eMatchmakeStatus::FAILED, 0 };

		MatchmakeResult result{};
		result.status  = eMatchmakeStatus::ASSIGNED;
		result.groupId = 1;

		return result;
	}
}
