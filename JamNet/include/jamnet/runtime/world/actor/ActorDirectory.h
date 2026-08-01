#pragma once

#include <jambase/JamMacro.h>

#include "jamnet/runtime/world/actor/ActorId.h"

#include <entt/entt.hpp>

#include <deque>
#include <vector>

namespace jam::net
{
	struct ActorSlot
	{
		uint32			generation = 0;
		entt::entity	entity     = entt::null;
		bool			reserved   = false;
		bool			retired    = false;
	};

	class ActorDirectory
	{
	public:
		// Binds an externally assigned canonical ActorId. Bound slots are reserved
		// from runtime allocation; repeated binding to the same entity is idempotent.
		bool					Bind(ActorId actorId, entt::entity entity);
		// Removes only the local representation and preserves the authoritative ID.
		bool					Unbind(ActorId actorId);
		// Runtime allocation/recycling. The server owns these operations once the
		// canonical identity migration is complete.
		ActorId					Allocate(entt::entity entity);
		bool					Release(ActorId actorId);
		entt::entity			Resolve(ActorId actorId) const;
		bool					Validate() const;
		void					Clear();
		static ActorId			MakeInitialId(uint32 slotIndex);
		static bool				IsInitialId(ActorId actorId);

	private:
		static constexpr uint32 kIndexBits      = 20;
		static constexpr uint32 kGenerationBits = 32 - kIndexBits;
		static constexpr uint32 kIndexMask      = (1u << kIndexBits) - 1u;
		static constexpr uint32 kGenerationMask = (1u << kGenerationBits) - 1u;

		static ActorId			MakeId(uint32 index, uint32 generation);
		static bool				Decode(ActorId actorId, OUT uint32& outIndex, OUT uint32& outGeneration);
		static uint32			NextGeneration(uint32 generation);
		void					ReserveInvalidSlot();
		void					GrowForBind(uint32 targetIndex);

	private:
		std::vector<ActorSlot>	m_slots;
		std::deque<uint32>		m_freeSlots;
	};
}
