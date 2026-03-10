#pragma once
#include "jampx/character/IAIDecisionModel.h"


namespace jam::px
{

	// ------------------------------------------------------------
	// IdleDecisionModel
	// ------------------------------------------------------------
	//
	// 기본 대기 모델:
	// - 이동 안 함
	// - 옵션으로 정면 유지 혹은 타겟 바라보기 가능
	//

	struct IdleDecisionConfig
	{
		bool faceTargetIfExists				= true;
		bool keepCurrentForwardIfNoTarget	= true;
	};

	class IdleDecisionModel final : public IAIDecisionModel
	{
	public:
		explicit IdleDecisionModel(const IdleDecisionConfig& cfg = {});

		void					Reset() override {}
		eAIDecisionStatus		Evaluate(const AIContext& ctx, OUT AIDesiredAction& desired) override;

	private:
		IdleDecisionConfig		m_cfg = {};
	};




	// ------------------------------------------------------------
	// ChaseDecisionModel
	// ------------------------------------------------------------
	//
	// 기본 추적 모델:
	// - target 이 있으면 target 쪽으로 이동
	// - stopDistance 안에 들어오면 정지
	// - 옵션으로 LOS 없을 때 실패 처리 가능
	// - 옵션으로 멀면 dash 요청 가능
	//

	struct ChaseDecisionConfig
	{
		float	stopDistance	= 1.25f;
		float	slowDistance	= 3.5f;         
		bool	requireLOS		= false;	// false: Tracking possible without LOS

		bool	enableDash		= false;
		float	dashMinDistance = 8.0f;

		bool	faceTarget		= true;
	};

	class ChaseDecisionModel final : public IAIDecisionModel
	{
	public:
		explicit ChaseDecisionModel(const ChaseDecisionConfig& cfg = {});

		void					Reset() override {}
		eAIDecisionStatus		Evaluate(const AIContext& ctx, AIDesiredAction& desired) override;

	private:
		ChaseDecisionConfig		m_cfg = {};
	};



	// ------------------------------------------------------------
	// PatrolDecisionModel
	// ------------------------------------------------------------
	//
	// 기본 순찰 모델:
	// - pathPoints 를 waypoint 리스트처럼 사용
	// - currentPathIndex 위치를 목표로 이동
	// - 도착하면 내부적으로 다음 point 로 진행
	// - loop / ping-pong 지원
	//

	enum class PatrolMode : uint8
	{
		Loop,
		PingPong
	};

	struct PatrolDecisionConfig
	{
		float		arriveDistance		= 0.5f;
		bool		faceMoveDirection	= true;
		PatrolMode	mode				= PatrolMode::Loop;
	};

	class PatrolDecisionModel final : public IAIDecisionModel
	{
	public:

		explicit PatrolDecisionModel(const PatrolDecisionConfig& cfg = {});

		void Reset() override;
		eAIDecisionStatus Evaluate(const AIContext& ctx, AIDesiredAction& desired) override;

	private:
		PatrolDecisionConfig m_cfg = {};
		int32 m_currentIdx = 0;
		int32 m_pingPongDir = 1;
	};




	// ------------------------------------------------------------
	// PriorityDecisionModel
	// ------------------------------------------------------------
	//
	// 우선순위 기반 합성 모델 (BT Selector 와 동일):
	// - 등록 순서대로 Evaluate
	// - Failed 가 아닌 첫 번째 결과를 채택
	// - 전부 Failed 이면 Failed 반환
	// - Reset() 은 모든 자식 모델에 전파
	//
	// 사용 예)
	//   priority.Add<ChaseDecisionModel>(...);   // 타겟 있으면 추적
	//   priority.Add<PatrolDecisionModel>(...);  // 없으면 순찰
	//   priority.Add<IdleDecisionModel>(...);    // 경로도 없으면 대기
	//

	class PriorityDecisionModel final : public IAIDecisionModel
	{
	public:
		template<typename T, typename... Args>
		T* Add(Args&&... args)
		{
			auto model = std::make_unique<T>(std::forward<Args>(args)...);
			T* ptr = model.get();
			m_models.push_back(std::move(model));
			return ptr;
		}

		void				Add(std::unique_ptr<IAIDecisionModel> model);

		void				Reset() override;
		eAIDecisionStatus	Evaluate(const AIContext& ctx, AIDesiredAction& desired) override;

	private:
		std::vector<std::unique_ptr<IAIDecisionModel>> m_models;
	};

}
