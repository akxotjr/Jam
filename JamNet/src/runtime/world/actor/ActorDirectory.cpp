#include "pch.h"
#include "jamnet/runtime/world/actor/ActorDirectory.h"

#include <unordered_set>

namespace jam::net
{
	bool ActorDirectory::Bind(ActorId actorId, entt::entity entity)
	{
		uint32 index = 0;
		uint32 generation = 0;
		if (entity == entt::null || !Decode(actorId, index, generation))
			return false;

		ReserveInvalidSlot();
		GrowForBind(index);

		ActorSlot& slot = m_slots[index];
		if (slot.retired)
			return false;
		if (slot.entity != entt::null)
			return slot.generation == generation && slot.entity == entity;

		slot.generation = generation;
		slot.entity = entity;
		slot.reserved = true;
		return true;
	}

	bool ActorDirectory::Unbind(ActorId actorId)
	{
		uint32 index = 0;
		uint32 generation = 0;
		if (!Decode(actorId, index, generation) || index >= m_slots.size())
			return false;

		ActorSlot& slot = m_slots[index];
		if (slot.entity == entt::null || slot.generation != generation || !slot.reserved)
			return false;

		slot.entity = entt::null;
		return true;
	}

	ActorId ActorDirectory::Allocate(entt::entity entity)
	{
		if (entity == entt::null)
			return ActorId::Invalid();

		ReserveInvalidSlot();

		uint32 index = 0;
		while (!m_freeSlots.empty())
		{
			const uint32 candidate = m_freeSlots.front();
			m_freeSlots.pop_front();
			if (candidate >= m_slots.size())
				continue;

			const ActorSlot& slot = m_slots[candidate];
			if (slot.entity == entt::null && !slot.reserved && !slot.retired)
			{
				index = candidate;
				break;
			}
		}

		if (index == 0)
		{
			index = static_cast<uint32>(m_slots.size());
			if (index > kIndexMask)
				return ActorId::Invalid();

			m_slots.emplace_back();
		}

		ActorSlot& slot = m_slots[index];
		if (slot.generation == 0)
			slot.generation = 1;
		slot.entity = entity;
		slot.reserved = false;
		return MakeId(index, slot.generation);
	}

	bool ActorDirectory::Release(ActorId actorId)
	{
		uint32 index = 0;
		uint32 generation = 0;
		if (!Decode(actorId, index, generation) || index >= m_slots.size())
			return false;

		ActorSlot& slot = m_slots[index];
		if (slot.entity == entt::null || slot.generation != generation || slot.reserved || slot.retired)
			return false;

		slot.entity = entt::null;
		const uint32 nextGeneration = NextGeneration(slot.generation);
		if (nextGeneration == 0)
		{
			slot.retired = true;
			return true;
		}

		slot.generation = nextGeneration;
		m_freeSlots.push_back(index);
		return true;
	}

	entt::entity ActorDirectory::Resolve(ActorId actorId) const
	{
		uint32 index = 0;
		uint32 generation = 0;
		if (!Decode(actorId, index, generation) || index >= m_slots.size())
			return entt::null;

		const ActorSlot& slot = m_slots[index];
		return (slot.generation == generation) ? slot.entity : entt::null;
	}

	bool ActorDirectory::Validate() const
	{
		if (m_slots.empty())
			return m_freeSlots.empty();

		if (m_slots[0].entity != entt::null || !m_slots[0].reserved)
			return false;

		std::unordered_set<uint32> freeSlots;
		freeSlots.reserve(m_freeSlots.size());
		for (const uint32 index : m_freeSlots)
		{
			if (index == 0 || index >= m_slots.size() || !freeSlots.insert(index).second)
				return false;

			const ActorSlot& slot = m_slots[index];
			if (slot.entity != entt::null || slot.reserved || slot.retired)
				return false;
		}

		std::unordered_set<entt::entity> liveEntities;
		for (uint32 index = 1; index < m_slots.size(); ++index)
		{
			const ActorSlot& slot = m_slots[index];
			if (slot.entity != entt::null && !liveEntities.insert(slot.entity).second)
				return false;
			if (slot.entity != entt::null && slot.generation == 0)
				return false;
			if ((slot.reserved || slot.retired || slot.entity != entt::null) && freeSlots.contains(index))
				return false;
		}

		return true;
	}

	void ActorDirectory::Clear()
	{
		m_slots.clear();
		m_freeSlots.clear();
	}

	ActorId ActorDirectory::MakeInitialId(uint32 slotIndex)
	{
		return MakeId(slotIndex, 1);
	}

	bool ActorDirectory::IsInitialId(ActorId actorId)
	{
		uint32 index = 0;
		uint32 generation = 0;
		return Decode(actorId, index, generation) && generation == 1;
	}

	ActorId ActorDirectory::MakeId(uint32 index, uint32 generation)
	{
		if (index == 0 || index > kIndexMask || generation == 0 || generation > kGenerationMask)
			return ActorId::Invalid();

		return ActorId((generation << kIndexBits) | index);
	}

	bool ActorDirectory::Decode(ActorId actorId, uint32& outIndex, uint32& outGeneration)
	{
		if (!actorId.IsValid())
			return false;

		outIndex = actorId.Value() & kIndexMask;
		outGeneration = (actorId.Value() >> kIndexBits) & kGenerationMask;
		return outIndex != 0 && outGeneration != 0;
	}

	uint32 ActorDirectory::NextGeneration(uint32 generation)
	{
		return generation >= kGenerationMask ? 0 : generation + 1u;
	}

	void ActorDirectory::ReserveInvalidSlot()
	{
		if (!m_slots.empty())
			return;

		m_slots.emplace_back(ActorSlot{
			.reserved = true,
			.retired = true,
		});
	}

	void ActorDirectory::GrowForBind(uint32 targetIndex)
	{
		if (targetIndex < m_slots.size())
			return;

		const uint32 firstNewIndex = static_cast<uint32>(m_slots.size());
		m_slots.resize(static_cast<size_t>(targetIndex) + 1);
		for (uint32 index = firstNewIndex; index < targetIndex; ++index)
			m_freeSlots.push_back(index);
	}
}
