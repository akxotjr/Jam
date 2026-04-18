#include "pch.h"
#include "BotInstance.h"

#include <cmath>

BotInstance::BotInstance(uint32 instanceId, uint64 userId, BotTrafficConfig config)
	: ClientInstance(instanceId, userId, ClientInstanceConfig{ .headlessNetWorld = config.headlessNetWorld })
	, m_cfg(config)
{
	m_type = eClientType::Bot;
}

void BotInstance::UpdateInput(float deltaTime)
{
	if (GetLocalObjectId() == px::INVALID_OBJ_ID)
		return;

	const float clampedDelta = std::clamp(deltaTime, 0.0f, 0.25f);
	const float prevElapsedSec = m_botElapsedSec;
	m_botElapsedSec   += clampedDelta;
	m_botActionAccSec += deltaTime;

	// ClientInputSystem latches a buf "current" input sample per world tick.
	// Compute the bot command from absolute patrol phase and publish one sample here.
	constexpr float k_sideSec		 = 2.2f;
	constexpr float k_turnSec		 = 0.35f;
	constexpr float turnStart		 = k_sideSec - k_turnSec;
	constexpr float k_patrolCycleSec = k_sideSec * 4.0f;

	float phase = std::fmod(m_botElapsedSec, k_patrolCycleSec);
	if (phase < 0.0f)
		phase += k_patrolCycleSec;

	const uint32 sideIndex	  = static_cast<uint32>(phase / k_sideSec) % 4;
	const float  sideT		  = phase - static_cast<float>(sideIndex) * k_sideSec;
	const float  turnAlphaRaw = (sideT - turnStart) / k_turnSec;
	const float  turnAlpha    = std::clamp(turnAlphaRaw, 0.0f, 1.0f);

	m_pitch = 0.0f;
	m_yaw   = static_cast<float>(sideIndex) * px::PI_DIV_TWO/* + turnAlpha * px::PI_DIV_TWO*/;

	uint32 inputFlags = px::INPUT_FORWARD;

	static constexpr float kJumpIntervalSec = 2.5f;
	const uint32 prevJumpBucket = static_cast<uint32>(std::floor(prevElapsedSec / kJumpIntervalSec));
	const uint32 currJumpBucket = static_cast<uint32>(std::floor(m_botElapsedSec / kJumpIntervalSec));
	//if (currJumpBucket != prevJumpBucket)
	//	inputFlags |= px::INPUT_JUMP;

	ControlCharacter(inputFlags, m_pitch, m_yaw);

	float actionHz = std::max(0.0f, m_cfg.reliableActionHz);
	if (m_cfg.burstPeriodSec > 0.0f)
	{
		const float burstWindow = std::max(0.0f, std::min(m_cfg.burstDurationSec, m_cfg.burstPeriodSec));
		const float phase		= std::fmod(m_botElapsedSec, m_cfg.burstPeriodSec);
		if (phase < burstWindow)
			actionHz *= std::max(1.0f, m_cfg.burstMultiplier);
	}

	if (actionHz <= 0.0f)
		return;

	const float actionPeriod = 1.0f / actionHz;
	while (m_botActionAccSec >= actionPeriod)
	{
		m_botActionAccSec -= actionPeriod;

		px::Vec3 shootDir(std::sin(m_yaw), 0.0f, std::cos(m_yaw));
		shootDir = shootDir.GetNormalized();
		if (shootDir.IsZero())
			shootDir = px::Vec3(0.0f, 0.0f, 1.0f);

		const px::Vec3 baseMuzzle(5.0f * static_cast<float>(GetInstanceId()), 0.9f, 0.0f);
		const px::Vec3 muzzlePos = baseMuzzle + shootDir * 0.45f;

		//SpawnBullet(muzzlePos, shootDir);
	}
}
