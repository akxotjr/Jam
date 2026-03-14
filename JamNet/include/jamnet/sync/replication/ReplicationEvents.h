#pragma once

#include <jampx/PhysicsTypes.h>



namespace jam::net
{

	struct MatchmakingSucceededEvent
	{
		uint64  userId = 0;
		uint32  groupId = 0;
	};

	/// @brief 렌더링 계층으로 전달할 액터 생성 이벤트 (일회성, 불변)
	struct RenderActorSpawnedEvent
	{
		uint64				userId = 0;
		uint32				spawnReqId = 0;
		uint32				objectId = 0;
		bool				isLocal = false;  // 내가 조종하는 액터인지
	};

	/// @brief 렌더링 계층으로 전달할액터 제거 이벤트
	struct RenderActorDespawnedEvent
	{
		uint64				userId = 0;
		uint32				objectId = 0;
	};


	struct RenderSamplesEvent
	{
		struct ActorSample
		{
			uint32							objectId = 0;
			bool							isLocal = false;  

			optional<px::RigidState>		rs{};
			optional<px::CharacterState>	cs{};
		};

		uint32					tick	  = 0;
		uint64					userId	  = 0;
		float					timestamp = 0.f;  // 이 샘플의 시간 (렌더러 보간용)
		vector<ActorSample>		actors;
	};

}