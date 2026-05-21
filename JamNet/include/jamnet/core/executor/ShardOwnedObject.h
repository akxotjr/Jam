#pragma once

#include "jamnet/core/executor/Mailbox.h"
#include "jamnet/core/executor/MailboxRef.h"

#include <functional>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace jam
{
	enum class eShardOwnedObjectState : uint8
	{
		Alive		= 0,
		Closing		= 1,
		Destroyed	= 2,
	};

	class IShardOwnedObject
	{
	public:
		virtual ~IShardOwnedObject() = default;

		virtual RuntimeId	GetShardOwnedRuntimeId() const = 0;
		virtual MailboxRef	GetShardOwnedMailboxRef() const = 0;
		virtual bool		BeginClose(eMailboxCloseMode mode, std::function<void()> onClosed) = 0;
	};

	template<typename T>
	struct RuntimeSlotTable
	{
		struct Entry
		{
			T			value		= {};
			bool		occupied	= false;
			uint16		index		= std::numeric_limits<uint16>::max();
			uint32		generation	= 0;
		};

		using IdType = RuntimeId;

		RuntimeId AllocId(uint16 shardIndex)
		{
			uint32 localIndex = 0;
			if (!freeIndices.empty())
			{
				localIndex = freeIndices.back();
				freeIndices.pop_back();
			}
			else
			{
				localIndex = static_cast<uint32>(entries.size());
				if (localIndex > std::numeric_limits<uint16>::max())
					return kInvalidRuntimeId;
				entries.resize(static_cast<size_t>(localIndex) + 1);
			}

			Entry& entry = entries[localIndex];
			entry.index = static_cast<uint16>(localIndex);
			entry.generation = (entry.generation + 1) == 0u ? 1u : entry.generation + 1;
			entry.value = {};
			entry.occupied = true;
			return MakeRuntimeId(shardIndex, entry.index, entry.generation);
		}

		void FreeId(RuntimeId id)
		{
			Entry* entry = FindEntry(id);
			if (!entry)
				return;

			entry->value = {};
			entry->occupied = false;
			freeIndices.push_back(entry->index);
		}

		bool Contains(RuntimeId id) const
		{
			return FindEntry(id) != nullptr;
		}

		T* Find(RuntimeId id)
		{
			Entry* entry = FindEntry(id);
			return entry ? &entry->value : nullptr;
		}

		const T* Find(RuntimeId id) const
		{
			const Entry* entry = FindEntry(id);
			return entry ? &entry->value : nullptr;
		}

		Entry* FindEntry(RuntimeId id)
		{
			const uint16 idx = GetRuntimeLocalIndex(id);
			const uint32 gen = GetRuntimeGeneration(id);
			if (idx >= entries.size() || gen == 0)
				return nullptr;

			Entry& entry = entries[idx];
			if (entry.index != idx || entry.generation != gen || !entry.occupied)
				return nullptr;
			return &entry;
		}

		const Entry* FindEntry(RuntimeId id) const
		{
			const uint16 idx = GetRuntimeLocalIndex(id);
			const uint32 gen = GetRuntimeGeneration(id);
			if (idx >= entries.size() || gen == 0)
				return nullptr;

			const Entry& entry = entries[idx];
			if (entry.index != idx || entry.generation != gen || !entry.occupied)
				return nullptr;
			return &entry;
		}

		std::vector<Entry>	entries		= {};
		std::vector<uint32>	freeIndices = {};
	};

	template<typename T>
	struct ShardOwnedObjectRefSlot
	{
		static_assert(std::is_base_of_v<IShardOwnedObject, std::remove_cv_t<T>>, "T must derive from IShardOwnedObject");

		T*			ptr		= nullptr;
		RuntimeId	ownerId = kInvalidRuntimeId;
		MailboxRef	mailbox = {};

		ShardOwnedObjectRefSlot() = default;
		explicit ShardOwnedObjectRefSlot(T* object)
		{
			Set(object);
		}

		void Set(T* object)
		{
			ptr		= object;
			ownerId = object ? object->GetShardOwnedRuntimeId() : kInvalidRuntimeId;
			mailbox = object ? object->GetShardOwnedMailboxRef() : MailboxRef{};
		}

		void Refresh()
		{
			if (!ptr)
			{
				Clear();
				return;
			}

			ownerId = ptr->GetShardOwnedRuntimeId();
			mailbox = ptr->GetShardOwnedMailboxRef();
		}

		void Clear()
		{
			ptr		= nullptr;
			ownerId = kInvalidRuntimeId;
			mailbox = {};
		}

		bool IsBound() const
		{
			return TryGet() != nullptr;
		}

		T* TryGet() const
		{
			if (!ptr || ownerId == kInvalidRuntimeId)
				return nullptr;
			if (ptr->GetShardOwnedRuntimeId() != ownerId)
				return nullptr;
			if (!mailbox.IsValid())
				return nullptr;
			return ptr;
		}

		T* TryGetRaw() const
		{
			return ptr;
		}

		bool TryPost(Job job) const
		{
			return mailbox.TryPost(std::move(job));
		}

		T* operator->() const
		{
			return TryGet();
		}

		T& operator*() const
		{
			return *TryGet();
		}
	};

	template<typename T>
	struct ShardOwnedObjectTable
	{
		struct Entry
		{
			std::unique_ptr<T>			object		= {};
			eShardOwnedObjectState		state		= eShardOwnedObjectState::Alive;
			uint16						index		= std::numeric_limits<uint16>::max();
			uint32						generation	= 0;
		};

		using IdType = RuntimeId;

		RuntimeId AllocId(uint16 shardIndex)
		{
			uint32 localIndex = 0;
			if (!freeIndices.empty())
			{
				localIndex = freeIndices.back();
				freeIndices.pop_back();
			}
			else
			{
				localIndex = static_cast<uint32>(entries.size());
				if (localIndex > std::numeric_limits<uint16>::max())
					return kInvalidRuntimeId;
				entries.resize(static_cast<size_t>(localIndex) + 1);
			}

			Entry& entry = entries[localIndex];
			entry.index		 = static_cast<uint16>(localIndex);
			entry.generation = (entry.generation + 1) == 0u ? 1u : entry.generation + 1;
			entry.object.reset();
			entry.state		 = eShardOwnedObjectState::Alive;
			return MakeRuntimeId(shardIndex, entry.index, entry.generation);
		}

		void FreeId(RuntimeId id)
		{
			Entry* entry = FindEntryAny(id);
			if (!entry)
				return;

			entry->object.reset();
			entry->state = eShardOwnedObjectState::Destroyed;
			freeIndices.push_back(entry->index);
		}

		bool Adopt(std::unique_ptr<T> object)
		{
			if (!object)
				return false;

			const RuntimeId id = object->GetShardOwnedRuntimeId();
			if (id == kInvalidRuntimeId)
				return false;

			Entry* entry = FindEntryAny(id);
			if (!entry || entry->object || entry->state == eShardOwnedObjectState::Closing)
				return false;

			entry->object = std::move(object);
			entry->state  = eShardOwnedObjectState::Alive;
			return true;
		}

		bool Contains(RuntimeId id) const
		{
			return FindEntryAny(id) != nullptr;
		}

		bool IsAlive(RuntimeId id) const
		{
			const Entry* entry = FindEntry(id);
			return entry && entry->object != nullptr;
		}

		T* Find(RuntimeId id)
		{
			Entry* entry = FindEntry(id);
			return entry ? entry->object.get() : nullptr;
		}

		const T* Find(RuntimeId id) const
		{
			const Entry* entry = FindEntry(id);
			return entry ? entry->object.get() : nullptr;
		}

		T* FindAny(RuntimeId id)
		{
			Entry* entry = FindEntryAny(id);
			return entry ? entry->object.get() : nullptr;
		}

		const T* FindAny(RuntimeId id) const
		{
			const Entry* entry = FindEntryAny(id);
			return entry ? entry->object.get() : nullptr;
		}

		MailboxRef FindMailboxRef(RuntimeId id) const
		{
			if (const T* object = Find(id))
				return object->GetShardOwnedMailboxRef();
			return {};
		}

		ShardOwnedObjectRefSlot<T> FindRef(RuntimeId id)
		{
			return ShardOwnedObjectRefSlot<T>(Find(id));
		}

		ShardOwnedObjectRefSlot<const T> FindRef(RuntimeId id) const
		{
			return ShardOwnedObjectRefSlot<const T>(Find(id));
		}

		ShardOwnedObjectRefSlot<T> FindAnyRef(RuntimeId id)
		{
			return ShardOwnedObjectRefSlot<T>(FindAny(id));
		}

		ShardOwnedObjectRefSlot<const T> FindAnyRef(RuntimeId id) const
		{
			return ShardOwnedObjectRefSlot<const T>(FindAny(id));
		}

		eShardOwnedObjectState GetState(RuntimeId id) const
		{
			if (const Entry* entry = FindEntryAny(id))
				return entry->state;
			return eShardOwnedObjectState::Destroyed;
		}

		bool BeginDestroy(RuntimeId id, eMailboxCloseMode mode = eMailboxCloseMode::Drain, std::function<void(RuntimeId)> onDestroyed = {})
		{
			Entry* entry = FindEntry(id);
			if (!entry || !entry->object)
				return false;

			if (entry->state != eShardOwnedObjectState::Alive)
				return false;

			T* object = entry->object.get();
			entry->state = eShardOwnedObjectState::Closing;

			const bool accepted = object->BeginClose(mode, [this, id, onDestroyed = std::move(onDestroyed)]()
				{
					FinalizeDestroy(id);
					if (onDestroyed)
						onDestroyed(id);
				});

			if (!accepted)
			{
				FinalizeDestroy(id);
				if (onDestroyed)
					onDestroyed(id);
				return false;
			}

			return true;
		}

		uint32 BeginDestroyAll(eMailboxCloseMode mode = eMailboxCloseMode::Drain, std::function<void(RuntimeId)> onDestroyed = {})
		{
			uint32 count = 0;
			std::vector<RuntimeId> pendingIds;
			pendingIds.reserve(entries.size());
			for (const Entry& entry : entries)
			{
				if (entry.state == eShardOwnedObjectState::Alive && entry.object)
					pendingIds.push_back(MakeRuntimeId(GetRuntimeShardIndex(entry.object->GetShardOwnedRuntimeId()), entry.index, entry.generation));
			}

			for (RuntimeId id : pendingIds)
			{
				if (BeginDestroy(id, mode, onDestroyed))
					++count;
			}
			return count;
		}

		bool FinalizeDestroy(RuntimeId id)
		{
			Entry* entry = FindEntryAny(id);
			if (!entry)
				return false;

			entry->object.reset();
			entry->state = eShardOwnedObjectState::Destroyed;
			freeIndices.push_back(entry->index);
			return true;
		}

		void Clear()
		{
			entries.clear();
			freeIndices.clear();
		}

		size_t Size() const
		{
			size_t count = 0;
			for (const Entry& entry : entries)
			{
				if (entry.object)
					++count;
			}
			return count;
		}

		bool Empty() const
		{
			return Size() == 0;
		}

		Entry* FindEntry(RuntimeId id)
		{
			Entry* entry = FindEntryAny(id);
			if (!entry || entry->state != eShardOwnedObjectState::Alive)
				return nullptr;
			return entry;
		}

		const Entry* FindEntry(RuntimeId id) const
		{
			const Entry* entry = FindEntryAny(id);
			if (!entry || entry->state != eShardOwnedObjectState::Alive)
				return nullptr;
			return entry;
		}

		Entry* FindEntryAny(RuntimeId id)
		{
			const uint16 idx = GetRuntimeLocalIndex(id);
			const uint32 gen = GetRuntimeGeneration(id);
			if (idx >= entries.size() || gen == 0)
				return nullptr;

			Entry& entry = entries[idx];
			if (entry.index != idx || entry.generation != gen)
				return nullptr;
			return &entry;
		}

		const Entry* FindEntryAny(RuntimeId id) const
		{
			const uint16 idx = GetRuntimeLocalIndex(id);
			const uint32 gen = GetRuntimeGeneration(id);
			if (idx >= entries.size() || gen == 0)
				return nullptr;

			const Entry& entry = entries[idx];
			if (entry.index != idx || entry.generation != gen)
				return nullptr;
			return &entry;
		}

		std::vector<Entry>	entries		= {};
		std::vector<uint32>	freeIndices = {};
	};


}
