#pragma once

namespace jam
{
	/*-----------------
		MemoryHeader
	------------------*/

	inline constexpr int32 SLIST_ALIGNMENT = 16;

	DECLSPEC_ALIGN(SLIST_ALIGNMENT)
	struct MemoryHeader : public SLIST_ENTRY
	{
		// [MemoryHeader][Data]
		MemoryHeader(int32 size) : allocSize(size) {}

		static void* AttachHeader(MemoryHeader* header, int32 size)
		{
			new(header)MemoryHeader(size); // placement new
			return reinterpret_cast<void*>(++header);
		}

		static MemoryHeader* DetachHeader(void* ptr)
		{
			MemoryHeader* header = reinterpret_cast<MemoryHeader*>(ptr) - 1;
			return header;
		}

		int32 allocSize;
	};



	/*-----------------
		MemoryPool
	------------------*/


	DECLSPEC_ALIGN(SLIST_ALIGNMENT)
	class MemoryPool
	{
	public:
		MemoryPool(int32 allocSize);
		~MemoryPool();

		void					Push(MemoryHeader* ptr);
		MemoryHeader*			Pop();

	private:
		SLIST_HEADER			m_header;
		int32					m_allocSize = 0;
		std::atomic<int32>		m_useCount = 0;
		std::atomic<int32>		m_reserveCount = 0;
	};
}

