#pragma once
#include "Allocator.h"


namespace jam::utils::memory
{
	class MemoryPool;

	inline constexpr int32 POOL_COUNT = (1024 / 32) + (1024 / 128) + (2048 / 256);
	inline constexpr int32 MAX_ALLOC_SIZE = 4096;




	class MemoryManager
	{
		DECLARE_SINGLETON(MemoryManager)

	public:
		void*	Allocate(int32 size);
		void	Release(void* ptr);

	private:
		std::vector<MemoryPool*>	_pools;
		MemoryPool*					_poolTable[MAX_ALLOC_SIZE + 1];
	};




	template<typename Type, typename... Args>
	Type* xnew(Args&&... args)
	{
		Type* memory = static_cast<Type*>(PoolAllocator::Alloc(sizeof(Type)));
		new(memory)Type(std::forward<Args>(args)...); // placement new
		return memory;
	}

	template<typename Type>
	void xdelete(Type* obj)
	{
		obj->~Type();
		PoolAllocator::Release(obj);
	}

	template<typename Type, typename... Args>
	std::shared_ptr<Type> MakeShared(Args&&... args)
	{
		return std::shared_ptr<Type>{ xnew<Type>(std::forward<Args>(args)...), xdelete<Type> };
	}
}

