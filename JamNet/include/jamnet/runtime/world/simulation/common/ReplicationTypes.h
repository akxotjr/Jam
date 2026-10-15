#pragma once
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/common/CharacterControlTypes.h"

namespace jam::net
{

	inline constexpr Duration SIMULATION_TICK_NS	 = 33'333'333_ns;
	inline constexpr float	SIMULATION_TICK_SEC		 = 0.033f;

	inline constexpr Duration FULLSNAPSHOT_INTERVAL_NS = 5_s;



	/// @brief Reconiliation 설정
	struct ReconcileConfig
	{
		float			positionErrorThreshold = 0.1f;		// 위치 오차 임계값 (m)
		float			rotationErrorThreshold = 0.05f;		// 회전 오차 임계값 (rad)
		float			smoothCorrectionAlpha  = 0.2f;		// 보정 계수
		uint32			maxReplayInputs		   = 16;		// 최대 재생 입력 수
	};


}
