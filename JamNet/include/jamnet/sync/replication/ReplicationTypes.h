#pragma once
#include "jamnet/sync/replication/NetActorComponents.h"

#include <jampx/PhysicsTypes.h>

namespace jam::net
{

	inline constexpr uint64 SIMULATION_TICK_NS		 = 33'333'333_ns;
	inline constexpr float	SIMULATION_TICK_SEC		 = 0.033f;

	inline constexpr uint64 FULLSNAPSHOT_INTERVAL_NS = 5_s;



	/// @brief 입력 커맨드 (Client/Server 공통)
	struct InputCmd
	{
		uint32						seq = 0;
		px::CharacterInput			input{};
	};


	/// @brief Reconiliation 설정
	struct ReconcileConfig
	{
		float			positionErrorThreshold = 0.1f;		// 위치 오차 임계값 (m)
		float			rotationErrorThreshold = 0.05f;		// 회전 오차 임계값 (rad)
		float			smoothCorrectionAlpha  = 0.2f;		// 보정 계수
		uint32			maxReplayInputs		   = 16;		// 최대 재생 입력 수
	};


	enum class eBucket : uint8
	{
		B0_MustSendFullMeta = 0,
		B1_MustSendFull		= 1,
		B2_HighDelta		= 2,
		B3_NormalDelta		= 3,
		B4_LowPriority		= 4,

		Count
	};

	struct Candidate
	{
		entt::entity	e			= entt::null;
		NetId			netId		= NetId::Invalid();
		bool			includeMeta	= false;
		bool			useFull		= false;
	};

}
