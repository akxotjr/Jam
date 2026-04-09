#include "pch.h"
#include "BotInstance.h"

#include <cmath>

BotInstance::BotInstance(uint32 instanceId, uint64 userId)
	: ClientInstance(instanceId, userId)
{

}

void BotInstance::UpdateInput(float deltaTime)
{
	m_botElapsedSec   += deltaTime;
	m_botInputAccSec  += deltaTime;
	m_botActionAccSec += deltaTime;

	const float inputHz		= std::max(1.0f, m_cfg.inputHz);
	const float inputPeriod = 1.0f / inputHz;

	while (m_botInputAccSec >= inputPeriod)
	{
		m_botInputAccSec -= inputPeriod;
		++m_botStep;

		uint32 inputFlags = px::INPUT_NONE;
		switch (m_botStep % 4)
		{
		case 0:  inputFlags = px::INPUT_FORWARD;	break;
		case 1:  inputFlags = px::INPUT_RIGHT;		break;
		case 2:  inputFlags = px::INPUT_BACKWARD;	break;
		default: inputFlags = px::INPUT_LEFT;		break;
		}

		if ((m_botStep % 30) == 0)
			inputFlags |= px::INPUT_JUMP;

		m_yaw += 0.03f;
		ControlCharacter(inputFlags, m_pitch, m_yaw);
	}

	float actionHz = std::max(0.0f, m_cfg.reliableActionHz);
	if (m_cfg.burstPeriodSec > 0.0f)
	{
		const float phase = std::fmod(m_botElapsedSec, m_cfg.burstPeriodSec);
		if (phase < m_cfg.burstDurationSec)
			actionHz *= std::max(1.0f, m_cfg.burstMultiplier);
	}

	if (actionHz <= 0.0f)
		return;

	const float actionPeriod = 1.0f / actionHz;
	while (m_botActionAccSec >= actionPeriod)
	{
		m_botActionAccSec -= actionPeriod;
		//SpawnBullet();
	}
}
