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

		double			GetServerTime() { return m_serverTime; }
		double			GetDeltaTime() { return m_deltaTime; }

	private:
		uint64			m_frequency = 0;
		uint64			m_prevCount = 0;
		double			m_deltaTime = 0.0f;
		double			m_serverTime = 0.0f;
	};
}

