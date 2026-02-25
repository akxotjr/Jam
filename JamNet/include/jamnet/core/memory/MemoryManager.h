#pragma once
#include "MemoryPool.h"

namespace jam
{
	class MemoryPool;

	inline constexpr int32 POOL_COUNT = (1024 / 32) + (1024 / 128) + (2048 / 256);
	inline constexpr int32 MAX_ALLOC_SIZE = 4096;


	class MemoryManager
	{
		DECLARE_SINGLETON(MemoryManager)

	public:
		void	Init();
		void	Shutdown();

		void*	Allocate(int32 size);
		void	Release(void* ptr);

	private:
		vector<MemoryPool*>		m_pools;
		MemoryPool*				m_poolTable[MAX_ALLOC_SIZE + 1];
	};



	template<typename Type, typename... Args>
	Type* xnew(Args&&... args)
	{
		void* raw = MemoryManager::Instance().Allocate(sizeof(Type));
		Type* obj = new(raw) Type(std::forward<Args>(args)...); // placement new
		return obj;
	}

	template<typename Type>
	void xdelete(Type* obj)
	{
		if (!obj) return;
		obj->~Type();
		MemoryManager::Instance().Release(obj);
	}


	template<typename Type>
	struct MemoryPoolDeleter
	{
		void operator()(Type* ptr) const noexcept
		{
			xdelete(ptr);
		}
	};

	template<typename Type, typename... Args>
	std::shared_ptr<Type> MakeShared(Args&&... args)
	{
		Type* raw = xnew<Type>(std::forward<Args>(args)...);
		return std::shared_ptr<Type>(raw, MemoryPoolDeleter<Type>{});
	}

	template<typename Type, typename... Args>
	std::unique_ptr<Type, MemoryPoolDeleter<Type>> MakeUnique(Args&&... args)
	{
		Type* raw = xnew<Type>(std::forward<Args>(args)...);
		return std::unique_ptr<Type, MemoryPoolDeleter<Type>>(raw);
	}

}




#define MEMORY_MANAGER jam::MemoryManager::Instance()
#define MEMORY_MANAGER_INIT() MEMORY_MANAGER.Init()