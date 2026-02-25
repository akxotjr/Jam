#pragma once


namespace jam::net
{

	enum class eMatchmakeStatus : uint8
	{
		ASSIGNED	= 0,
		WAITING		= 1,
		FAILED		= 2,
	};


	struct MatchmakeRequest
	{
		uint64 principalId = 0;
	};

	struct MatchmakeResult
	{
		eMatchmakeStatus	status = eMatchmakeStatus::FAILED;
		uint32				groupId = 0;		// ASSIGNED일때만 유효. 0은 미배정 
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
