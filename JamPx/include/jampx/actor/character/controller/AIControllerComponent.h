#pragma once

#include "jampx/actor/character/controller/ICharacterController.h"
#include "jampx/actor/character/controller/IAIDecisionModel.h"

namespace jam::px
{
	class AIControllerComponent : public ICharacterController
	{
	public:

		void					SetModel(std::unique_ptr<IAIDecisionModel> model) { m_model = std::move(model); }
		void					UpdateContext(const AIContext& ctx) { m_ctx = ctx; }
		MoveIntent				BuildIntent(float dt) override;
		eCharacterControlType	GetType() const override { return eCharacterControlType::AI; }

	private:
		std::unique_ptr<IAIDecisionModel>	m_model = nullptr;
		AIContext							m_ctx	= {};
	};


} // namespace jam::px
