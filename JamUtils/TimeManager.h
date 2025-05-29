#pragma once
#include "ISingletonLayer.h"

namespace jam::utils
{
	class TimeManager : public ISingletonLayer<TimeManager>
	{
		friend class jam::ISingletonLayer<TimeManager>;

	public:
		void			Init() override;
		void			Shutdown() override;

		void			Update();

		double			GetCurrentTime() { return m_currentTime; }
		double			GetDeltaTime() { return m_deltaTime; }

	private:
		uint64			m_frequency = 0;
		uint64			m_prevCount = 0;
		double			m_deltaTime = 0.0f;
		double			m_currentTime = 0.0f;
	};
}

