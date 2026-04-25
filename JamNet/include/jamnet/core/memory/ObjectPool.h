#pragma once


#include "jamnet/core/memory/DefaultAllocator.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>


namespace jam
{
	inline constexpr size_t OBJECT_POOL_ALIGNMENT = MEMORY_ALLOCATION_ALIGNMENT;

	template<typename Type>
	class ObjectPool
	{
	private:
		static constexpr size_t k_nodeAlignment = std::max<size_t>(OBJECT_POOL_ALIGNMENT, alignof(Type));

		struct alignas(k_nodeAlignment) Node
		{
			SLIST_ENTRY entry;
			alignas(Type) std::byte storage[sizeof(Type)];

			Type* Object()
			{
				return std::launder(reinterpret_cast<Type*>(storage));
			}

			static Node* FromObject(Type* obj)
			{
				auto* bytes = reinterpret_cast<std::byte*>(obj);
				return reinterpret_cast<Node*>(bytes - offsetof(Node, storage));
			}
		};

		static_assert(std::is_standard_layout_v<Node>);

		struct State
		{
			State()
			{
				::InitializeSListHead(&freeList);
			}

			~State()
			{
				while (auto* entry = ::InterlockedPopEntrySList(&freeList))
				{
					FreeAligned(reinterpret_cast<Node*>(entry));
				}
			}

			SLIST_HEADER				freeList;
			std::atomic<std::uint64_t>	allocCount = 0;
			std::atomic<std::uint64_t>	reuseCount = 0;
			std::atomic<std::uint64_t>	freeCount  = 0;
		};

	public:
		template<typename... Args>
		static Type* Pop(Args&&... args)
		{
			Node* node = nullptr;
			if (auto* entry = ::InterlockedPopEntrySList(&s_state.freeList))
			{
				node = reinterpret_cast<Node*>(entry);
				s_state.reuseCount.fetch_add(1, std::memory_order_relaxed);
			}
			else
			{
				node = static_cast<Node*>(AllocAligned(sizeof(Node), k_nodeAlignment));
				if (!node)
					throw std::bad_alloc{};
				s_state.allocCount.fetch_add(1, std::memory_order_relaxed);
			}

			Type* obj = node->Object();
			new(obj) Type(std::forward<Args>(args)...);
			return obj;
		}

		static void Push(Type* obj)
		{
			if (!obj)
				return;

			obj->~Type();
			Node* node = Node::FromObject(obj);
			::InterlockedPushEntrySList(&s_state.freeList, &node->entry);
			s_state.freeCount.fetch_add(1, std::memory_order_relaxed);
		}

		template<typename... Args>
		static std::shared_ptr<Type> MakeShared(Args&&... args)
		{
			return std::shared_ptr<Type>(Pop(std::forward<Args>(args)...), &ObjectPool<Type>::Push);
		}

	private:
		inline static State s_state{};
	};
}
