#pragma once
#include "jamnet/runtime/world/simulation/common/ReplicationTypes.h"
#include "jamnet/runtime/world/simulation/common/CharacterControlResolver.h"

#include <cstddef>
#include <map>

namespace jam::net
{
	class ServerInputSystem
	{
public:
		ServerInputSystem(entt::registry& world);
		~ServerInputSystem() = default;

		void									Init();
		void									Tick();

		void									EnqueueInput(uint64 userId, const CharacterControlCommand& cmd);
		void									RemoveUser(uint64 userId);
		void									MarkInputApplied(uint64 userId);
		uint32									LastAppliedSeq(uint64 userId) const;
		uint32									LastAppliedControlRevision(uint64 userId) const;

private:
		void									QueuePendingInput(uint64 userId, const CharacterControlCommand& cmd);
		CharacterControlCommand					SelectInputForTick(uint64 userId);

private:
		static constexpr std::size_t kMaxPendingInputsPerUser = 256;
		//struct MoveToDiagnosticState
		//{
		//	px::Vec3 target = px::Vec3::Zero();
		//	px::Vec3 lastPosition = px::Vec3::Zero();
		//	uint32 lastSampleTick = 0;
		//	uint8 stationarySamples = 0;
		//	bool initialized = false;
		//	bool reported = false;
		//};

		entt::registry&							m_world;

		// Preserve client sequence order so one server simulation step consumes
		// exactly one logical client input slot. Missing slots are synthesized from
		// the held continuous state once a newer sequence proves that the slot exists.
		std::unordered_map<uint64, std::map<uint32, CharacterControlCommand>>	m_pendingInputs;
		std::unordered_map<uint64, CharacterControlCommand>				m_currentInputs;
		std::unordered_map<uint64, CharacterControlCommand>				m_appliedInputs;
		std::unordered_map<uint64, uint32>								m_latestControlRevisions;
		//std::unordered_map<uint64, MoveToDiagnosticState>				m_moveToDiagnostics;
		CharacterControlResolveConfig					m_controlResolveConfig = {};
	};
}
