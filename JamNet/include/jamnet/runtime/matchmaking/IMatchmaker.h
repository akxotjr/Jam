#pragma once


namespace jam::net
{

	enum class eMatchmakeStatus : uint8
	{
		Assigned	= 0,
		Waiting		= 1,
		Failed		= 2,
	};


	struct MatchmakeRequest
	{
		uint64				principalId = 0;
	};

	struct MatchmakeResult
	{
		eMatchmakeStatus	status		= eMatchmakeStatus::Failed;
		uint32				groupId		= 0;		// ASSIGNED일때만 유효. 0은 미배정 
	};

	class ServerNetworkManager;

	class IMatchmaker
	{
	public:
		virtual ~IMatchmaker() = default;

		virtual void			Init(ServerNetworkManager* owner) = 0;
		virtual MatchmakeResult RequestGroupId(const MatchmakeRequest& req) = 0;
	};
}
