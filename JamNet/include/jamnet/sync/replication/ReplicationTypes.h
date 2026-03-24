#pragma once

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


	/// @brief 클라이언트 예측 상태 (Reconciliation 비교용)
	//struct PredictedState
	//{
	//	uint32						inputSeq = 0;
	//	px::CharacterState			state{};
	//};

	/// @brief Reconiliation 설정
	struct ReconcileConfig
	{
		float			positionErrorThreshold = 0.1f;	// 위치 오차 임계값 (m)
		float			rotationErrorThreshold = 0.05f;	// 회전 오차 임계값 (rad)
		float			smoothCorrectionAlpha = 0.2f;		// 보정 계수
		uint32			maxReplayInputs = 64;			// 최대 재생 입력 수
	};

}
