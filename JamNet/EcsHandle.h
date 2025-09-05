#pragma once

namespace jam::net::ecs
{
	struct ReliabilityStore;

	struct EcsHandle
	{
		uint32 idx = UINT32_MAX;
		uint32 gen = 0;
		bool valid() const { return idx != UINT32_MAX; }
		static EcsHandle invalid() { return {}; }
		bool operator==(const EcsHandle& other) const { return idx == other.idx && gen == other.gen; }
	};

	template<typename T>
	class EcsHandlePool
	{
		struct Slot
		{
			uint32 gen = 1;
			bool alive = false;
			T val{};
			uint32 nextFree = UINT32_MAX;
		};

	public:
		EcsHandle alloc()
		{
			if (m_freeHead != UINT32_MAX) {
				const uint32_t i = m_freeHead;
				m_freeHead = m_slots[i].nextFree;
				m_slots[i].alive = true;
				return EcsHandle{ i, m_slots[i].gen };
			}
			Slot s{};
			s.alive = true;
			m_slots.emplace_back(std::move(s));
			return EcsHandle{ static_cast<uint32_t>(m_slots.size() - 1), m_slots.back().gen };
		}

		void free(EcsHandle h)
		{
			if (!isAlive(h)) return;
			auto& sl = m_slots[h.idx];
			sl.alive = false;
			sl.gen++;
			sl.nextFree = m_freeHead;
			m_freeHead = h.idx;
			sl.val = T{}; // 필요시 정리
		}

		bool isAlive(EcsHandle h) const
		{
			return h.valid() && h.idx < m_slots.size() && m_slots[h.idx].alive && m_slots[h.idx].gen == h.gen;
		}

		T* get(EcsHandle h)
		{
			return isAlive(h) ? &m_slots[h.idx].val : nullptr;
		}
		const T* get(EcsHandle h) const
		{
			return isAlive(h) ? &m_slots[h.idx].val : nullptr;
		}

		size_t size() const { return m_slots.size(); }

	private:
		std::vector<Slot>		m_slots;
		uint32_t				m_freeHead = UINT32_MAX;
	};





	struct EcsHandlePools
	{
		EcsHandlePool<ReliabilityStore> reliability;
		
	};

}
