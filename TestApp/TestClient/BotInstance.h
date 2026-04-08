#pragma once
#include "ClientInstance.h"

struct BotTrafficConfig
{
	bool	enabled				= false;
	float	inputHz				= 30.0f;	// UNRELIABLE_SEQUENCED 경로
	float	reliableActionHz	= 8.0f;		// SpawnBullet 기반 reliable 경로
	float	burstPeriodSec		= 10.0f;
	float	burstDurationSec	= 2.0f;
	float	burstMultiplier		= 4.0f;
};

class BotInstance : public ClientInstance
{
public:
	BotInstance(uint32 instanceId, uint64 userId);
	~BotInstance() override = default;

protected:
	void UpdateInput(float deltaTime) override;


private:
	BotTrafficConfig	m_cfg				= {};
	float				m_botElapsedSec		= 0.0f;
	float				m_botInputAccSec	= 0.0f;
	float				m_botActionAccSec	= 0.0f;
	uint32				m_botStep			= 0;
};

