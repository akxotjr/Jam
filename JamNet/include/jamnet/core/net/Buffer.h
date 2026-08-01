#pragma once

#include <jambase/JamMacro.h>
#include <jambase/JamAssert.h>
#include <jambase/CacheLine.h>

#include <vector>
#include <array>
#include <atomic>

namespace jam::net
{
	
	class BufferPool;

	class BufferBlock
	{
	public:
		explicit BufferBlock(BufferPool* owner);
		BufferBlock(const BufferBlock&) = delete;
		BufferBlock& operator=(const BufferBlock&) = delete;

		static constexpr uint32 k_blockSize    = 16 * 1024;
		static constexpr uint32 k_invalidIndex = UINT32_MAX;
		static constexpr uint32 k_maxSliceStates = k_blockSize;

	public:

		void							AddRef();
		void							Release();
		void							Reset();

		bool							IsExclusive()	const { return m_refCount.load(std::memory_order_relaxed) == 1; }
		uint32							PoolIndex()		const { return m_poolIndex; }
		bool							IsInFreeList()  const { return m_inFreeList; }
		bool							IsOpen()		const { return m_open; }
		uint32							UsedSize()		const { return m_used; }
		uint32							FreeSize()		const { return k_blockSize - m_used; };

		BYTE*							RawData()		{ return m_storage.data(); }
		const BYTE*						RawData() const { return m_storage.data(); }

		bool							TryReserve(uint32 bytes, uint32 alignment, OUT uint32& offset, OUT uint32& sliceStateIndex);
		void							Commit(uint32 endOffset);
		void							AbortReserve();

	private:
		friend class BufferPool;
		friend class BufWriter;
		friend struct BufferSlice;

		uint32							CreateSliceState(bool closed);
		bool							IsSliceClosed(uint32 sliceStateIndex) const;
		void							SetSliceClosed(uint32 sliceStateIndex, bool closed);

		std::atomic<uint32>					m_refCount	 = 1;
		BufferPool*							m_owner		 = nullptr;

		alignas(std::max_align_t) 
		std::array<BYTE, k_blockSize>		m_storage	 = {};
													 
		uint32								m_used		 = 0;
		bool								m_open		 = false;

		// pool bookkeeping
		uint32								m_poolIndex  = k_invalidIndex;
		bool								m_inFreeList = false;

		std::atomic<uint32>					m_nextSliceStateIndex = 0;
		std::array<std::atomic<uint8>, k_maxSliceStates> m_sliceClosedStates = {};
	};



	/**
	 *
	 * BufferSlice Data Layout
	 *
	 *							(Begin() ~ End())
	 *	|--------------------------- Capacity() ----------------------------|
	 *	^                                                                   ^
	 *	reserved begin														reserved end
	 *	^                ^                                                  ^
	 *	Begin()  ^        Data()								  ^			End()
	 *	         Head()											  Tail()
	 *	+----------------+-------------------------------+-----------------+
	 *	|    headroom    |             data              |    tailroom     |
	 *	+----------------+-------------------------------+-----------------+
	 *	
	 *			|-------------- Size() ----------------------------|
	 *                    (Head() ~ Tail())
	 *               
	 * invariant: begin <= head <= data <= tail <= end
	 * 
	 *	[ Begin ~ Head ) = unused prefix / headroom
	 *	[ Head  ~ Data ) = headers
	 *	[ Data  ~ Tail ) = payload
	 *	[ Tail  ~ End  ) = tailroom
	 *
	 */
	struct BufferSlice
	{

		BufferBlock* block	= nullptr;

		// all offsets are absolute offsets inside BufferBlock

		uint32		begin	= 0;   // reserved begin
		uint32		head	= 0;   // visible packet begin
		uint32		data	= 0;   // payload begin
		uint32		tail	= 0;   // visible packet end
		uint32		end		= 0;   // reserved end

		uint32		closedStateIndex = BufferBlock::k_invalidIndex;




		bool		IsValid()  const { return block != nullptr; }
		bool		IsClosed() const { return block && block->IsSliceClosed(closedStateIndex); }
		bool		IsExclusive() const { return block && block->IsExclusive(); }
		void		Validate() const;

		BYTE*		Begin() const { return block ? (block->RawData() + begin) : nullptr; }
		BYTE*		Head()	const { return block ? (block->RawData() + head)  : nullptr; }
		BYTE*		Data()	const { return block ? (block->RawData() + data)  : nullptr; }
		BYTE*		Tail()	const { return block ? (block->RawData() + tail)  : nullptr; }
		BYTE*		End()	const { return block ? (block->RawData() + end)   : nullptr; }

		uint32		Capacity()		const { Validate(); return end  - begin; }
		uint32		Headroom()		const { Validate(); return head - begin; }
		uint32		HeaderSize()	const { Validate(); return data - head;  }
		uint32		PayloadSize()	const { Validate(); return tail - data;  }
		uint32		Size()			const { Validate(); return tail - head;  }
		uint32		CommittedSize() const { Validate(); return tail - begin; }
		uint32		Tailroom()		const { Validate(); return end  - tail;  }

		bool		Empty()			const { return Size() == 0; }

		// reserve 전체를 payload 중심 초기 상태로 설정
		// begin <= head == data == tail <= end
		void ResetWindow(uint32 headroom = 0);

		// payload append: [Data ~ Tail) 뒤에 추가
		bool TryAppendPayload(uint32 bytes);

		// header prepend: [Head ~ Data) 앞쪽에 헤더 확장
		// payload 시작점(data)은 그대로 유지
		bool TryPrependHeader(uint32 bytes);

		// header 안에서 앞부분을 제거하고 싶을 때
		void RemoveHeaderPrefix(uint32 bytes);

		// payload prefix 소비
		void RemovePayloadPrefix(uint32 bytes);

		// payload suffix 제거
		void RemovePayloadSuffix(uint32 bytes);

		// header + payload 전체 visible 구간 뒤에서 제거
		void RemoveSuffix(uint32 bytes);

		// payload 시작점을 현재 head로 맞추고 싶을 때
		// (헤더가 없는 pure payload buffer로 재정렬)
		void CollapseHeader();

		// 현재 visible 구간 [head, tail) 전체를 하나의 view로 자름
		BufferSlice SliceVisible(uint32 subOffset, uint32 subSize) const;

		// payload 구간 [data, tail) 기준으로 자름
		BufferSlice SlicePayload(uint32 subOffset, uint32 subSize) const;

		// commit 길이는 [begin, tail) 사용량으로 본다.
		// 즉 reserved 앞 unused prefix는 없고,
		// 실제 packet visible 구간 + 그 앞 reserved prefix까지 block 소비 처리
		void Close();

		// reserved 전체 중 실제로 얼마를 소비할지 직접 지정
		void CloseWithCommit(uint32 committedBytes);

		// reserved 영역을 block에 확정하되, 이 slice는 계속 mutable storage로 사용한다.
		void CommitReserved(uint32 committedBytes);

		void Abort();

	};


	class OwnedBufferSlice
	{
	public:
		OwnedBufferSlice() = default;
		explicit OwnedBufferSlice(const BufferSlice& slice);
		explicit OwnedBufferSlice(BufferSlice&& slice);
		OwnedBufferSlice(const OwnedBufferSlice& rhs);
		OwnedBufferSlice(OwnedBufferSlice&& rhs) noexcept;

		OwnedBufferSlice& operator=(const OwnedBufferSlice& rhs);
		OwnedBufferSlice& operator=(OwnedBufferSlice&& rhs) noexcept;

		~OwnedBufferSlice() { ReleaseIfNeeded(); }

		const BufferSlice&	Get() const { return m_slice; }
		BufferSlice&		Get()	    { return m_slice; }

		const BufferSlice*	operator->() const { return &m_slice; }
		BufferSlice*		operator->()	   { return &m_slice; }

		const BufferSlice&	operator*() const { return m_slice; }
		BufferSlice&		operator*()		  { return m_slice; }

		bool				IsValid() const { return m_slice.IsValid(); }
		void				Reset();

	private:
		void				AddRefIfNeeded();
		void				ReleaseIfNeeded();

	private:
		BufferSlice			m_slice = {};
	};
	

	struct BufferPoolMetrics
	{
		std::atomic<uint64_t> acquireCalls		 = 0;
		std::atomic<uint64_t> recycleCalls		 = 0;

		std::atomic<uint64_t> acquireLockWait_ns = 0;
		std::atomic<uint64_t> recycleLockWait_ns = 0;
		std::atomic<uint64_t> globalLockCount	 = 0;

		std::atomic<uint64_t> slowAllocCount	 = 0;
		std::atomic<uint64_t> refillCount		 = 0;
		std::atomic<uint64_t> refillBlockCount	 = 0;
		std::atomic<uint64_t> spillCount		 = 0;
		std::atomic<uint64_t> spillBlockCount	 = 0;
		std::atomic<uint64_t> directRecycleCount = 0;
		std::atomic<uint64_t> peakTlsFreeCount	 = 0;
	};

	struct BufferPoolConfig
	{
		uint32 initialBlocks = 64;
		uint32 tlsLowWatermark = 8;
		uint32 tlsHighWatermark = 64;
		uint32 batchSize = 16;
		uint64 logEveryGlobalLocks = 1024;
		const char* debugName = "BufferPool";
	};

	class BufferPool
	{
	public:
		BufferPool();
		explicit BufferPool(BufferPoolConfig config);
		~BufferPool();

		BufferBlock*	Acquire();
		void			Recycle(BufferBlock* block);

		static void     FlushThreadLocalCache();

#if _DEBUG
		uint32			TotalBlockCount()  const { return static_cast<uint32>(m_blocks.size()); }
		uint32			FreeBlockCount()   const { return static_cast<uint32>(m_freeIndices.size()); }
		uint32			OutstandingCount() const { return static_cast<uint32>(m_blocks.size() - m_freeIndices.size()); }
#endif

	private:
		friend class BufWriter;

		using TlsFreeList = std::vector<BufferBlock*>;
		struct TlsFreeCaches
		{
			std::unordered_map<BufferPool*, TlsFreeList> entries;
			~TlsFreeCaches();
		};
		static thread_local TlsFreeCaches tl_freeCaches;

	private:
		TlsFreeList&	GetTlsFreeList();
		BufferBlock*	PopGlobalLocked();
		BufferBlock*	AllocateBlockLocked(bool freeList);
		void			PreallocateLocked(uint32 count);
		void			RefillLocalCache(TlsFreeList& local);
		void			SpillLocalCache(TlsFreeList& local, uint32 keepCount);
		void            RecycleGlobal(BufferBlock* block);
		void			UpdatePeakTlsFree(size_t count);
		void			LogMetricsIfNeeded(uint64 globalLockCount);


	private:
		mutable std::mutex          m_lock;
		std::vector<BufferBlock*>	m_blocks;			// ownership
		std::vector<uint32>			m_freeIndices;		// availability

		BufferPoolMetrics			m_metrics;
		BufferPoolConfig			m_config;
	};

	enum class eNetBufferPoolKind : uint8
	{
		Packet,
		TcpIo,
		UdpClientIo,
		UdpServerIo,
		PiggybackAck,
		Clone,
	};

	BufferPool&	GetNetBufferPool(eNetBufferPoolKind kind);
	
	void		FlushNetBufferThreadLocalCaches();

	inline bool IsSupportedBufferAlignment(uint32 alignment)
	{
		return alignment != 0
			&& (alignment & (alignment - 1)) == 0
			&& alignment <= alignof(std::max_align_t);
	}


	class BufWriter
	{
	public:
		explicit BufWriter(BufferPool& pool) : m_pool(pool) {}

		// reserveBytes 전쳬 예약 후, visible payload 시작점을 begin + initialHeadroom 으로 둔다.
		BufferSlice Open(uint32 reserveBytes, uint32 initialHeadroom, uint32 alignment = alignof(std::max_align_t));
		BufferSlice OpenForPacket(uint32 payloadSize, uint32 headerSize, uint32 alignment = alignof(std::max_align_t));
		BufferSlice OpenForPayload(uint32 payloadSize, uint32 alignment = alignof(std::max_align_t));


		static void FlushThreadLocal()
		{
			for (auto& block : tl_currents.entries | std::views::values)
			{
				if (block && block->m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
				{
					JAM_ASSERT(block->m_owner);
					block->Reset();
					block->m_owner->RecycleGlobal(block);
				}
				block = nullptr;
			}
			tl_currents.entries.clear();
		}

	private:
		BufferBlock*& CurrentTLBlock() { return tl_currents.entries[&m_pool]; }


	private:
		BufferPool&							m_pool;
		struct TlsCurrentBlocks
		{
			std::unordered_map<BufferPool*, BufferBlock*> entries;
			~TlsCurrentBlocks();
		};
		static thread_local TlsCurrentBlocks tl_currents;
	};


	class BufferChain
	{
	public:
		void								Add(BufferSlice slice) { m_parts.push_back(slice); }
		uint32								TotalSize() const;
		const std::vector<BufferSlice>&		Parts() const { return m_parts; }

	private:
		std::vector<BufferSlice>			m_parts;
	};


	class OwnedBufferChain
	{
	public:
		OwnedBufferChain() = default;
		OwnedBufferChain(const OwnedBufferChain& rhs);
		OwnedBufferChain(OwnedBufferChain&& rhs) noexcept;

		OwnedBufferChain& operator=(const OwnedBufferChain& rhs);
		OwnedBufferChain& operator=(OwnedBufferChain&& rhs) noexcept;

		~OwnedBufferChain() { Clear(); }

		void							Add(const BufferSlice& slice);
		void							Add(BufferSlice&& slice);
		void							Add(const OwnedBufferSlice& slice) { Add(slice.Get()); }

		void							Reserve(size_t n) { m_parts.reserve(n); }

		void							Clear();

		bool							Empty() const { return m_parts.empty(); }
		size_t							Count() const { return m_parts.size(); }
		uint32							TotalSize() const;

		const std::vector<BufferSlice>& Parts() const { return m_parts; }
		std::vector<BufferSlice>&		Parts()		  { return m_parts; }

		const BufferSlice&				operator[](size_t idx) const { return m_parts[idx]; }
		BufferSlice&					operator[](size_t idx)		 { return m_parts[idx]; }

	private:
		std::vector<BufferSlice>		m_parts;
	};
	



	template <typename T>
	T* PrependPod(BufferSlice& buf)
	{
		JAM_ASSERT_MSG(std::is_trivially_copyable_v<T>, "T must be trivally copyable");

		buf.Validate();
		JAM_ASSERT(!buf.IsClosed());

		constexpr uint32 k_align = static_cast<uint32>(alignof(T));
		constexpr uint32 k_size = static_cast<uint32>(sizeof(T));

		JAM_ASSERT(buf.head >= buf.begin);
		JAM_ASSERT(buf.head <= buf.data);
		JAM_ASSERT(buf.Headroom() >= k_size);

		// 현재 head 앞쪽으로 T 하나를 넣되, T 시작 주소를 alignof(T)에 맞춘다.
		const uint32 candidateBegin = buf.head - k_size;
		const uint32 alignedBegin = static_cast<uint32>(AlignDown(static_cast<size_t>(candidateBegin), static_cast<size_t>(k_align)));

		JAM_ASSERT(alignedBegin <= candidateBegin);
		JAM_ASSERT(alignedBegin >= buf.begin);

		const uint32 required = buf.head - alignedBegin;
		JAM_ASSERT(buf.Headroom() >= required);

		buf.head = alignedBegin;
		return reinterpret_cast<T*>(buf.Head());
	}

	template <typename T>
	T* AppendPod(BufferSlice& buf)
	{
		JAM_ASSERT_MSG(std::is_trivially_copyable_v<T>, "T must be trivally copyable");

		buf.Validate();
		JAM_ASSERT(!buf.IsClosed());

		constexpr uint32 k_align = static_cast<uint32>(alignof(T));
		constexpr uint32 k_size  = static_cast<uint32>(sizeof(T));

		const uint32 alignedTail = static_cast<uint32>(AlignUp(static_cast<size_t>(buf.tail), static_cast<size_t>(k_align)));

		const uint32 padding = alignedTail - buf.tail;
		JAM_ASSERT(buf.Tailroom() >= padding + k_size);

		buf.tail = alignedTail;

		BYTE* writePtr = buf.Tail();
		JAM_ASSERT(buf.TryAppendPayload(k_size));

		return reinterpret_cast<T*>(writePtr);
	}

	inline BYTE* WritePayload(BufferSlice& buf, const void* src, uint32 bytes)
	{
		buf.Validate();
		JAM_ASSERT(src != nullptr || bytes == 0);
		JAM_ASSERT(!buf.IsClosed());

		BYTE* writePtr = buf.Tail();  
		JAM_ASSERT_OR_RETURN_VALUE(buf.TryAppendPayload(bytes), nullptr);

		if (bytes > 0) std::memcpy(writePtr, src, bytes);

		return writePtr;
	}

	inline BYTE* WriteHeader(BufferSlice& buf, const void* src, uint32 bytes)
	{
		buf.Validate();
		JAM_ASSERT(src != nullptr || bytes == 0);
		JAM_ASSERT(!buf.IsClosed());

		JAM_ASSERT_OR_RETURN_VALUE(buf.TryPrependHeader(bytes), nullptr);
		BYTE* writePtr = buf.Head();

		if (bytes > 0) std::memcpy(writePtr, src, bytes);

		return writePtr;
	}

	template <typename T>
	BYTE* WriteHeader(BufferSlice& buf, const T& value, uint32 bytes = static_cast<uint32>(sizeof(T)))
	{
		JAM_ASSERT_MSG(std::is_trivially_copyable_v<T>, "T must be trivally copyable");
		JAM_ASSERT(bytes <= sizeof(T));
		return WriteHeader(buf, static_cast<const void*>(&value), bytes);
	}

	inline BYTE* ReservePayload(BufferSlice& buf, uint32 bytes)
	{
		buf.Validate();
		JAM_ASSERT(!buf.IsClosed());

		BYTE* writePtr = buf.Tail();
		JAM_ASSERT(buf.TryAppendPayload(bytes));
		return writePtr;
	}

	inline BYTE* ReserveAlignedPayload(BufferSlice& buf, uint32 bytes, uint32 alignment)
	{
		buf.Validate();
		JAM_ASSERT(!buf.IsClosed());
		JAM_ASSERT(IsSupportedBufferAlignment(alignment));

		const uint32 alignedTail = static_cast<uint32>(AlignUp(static_cast<size_t>(buf.tail), static_cast<size_t>(alignment)));

		const uint32 padding = alignedTail - buf.tail;
		JAM_ASSERT(buf.Tailroom() >= padding + bytes);

		buf.tail = alignedTail;
		BYTE* ptr = buf.Tail();
		JAM_ASSERT(buf.TryAppendPayload(bytes));
		return ptr;
	}

	template <typename T>
	void PrependPod(BufferSlice& buf, const T& value)
	{
		T* dst = PrependPod<T>(buf);
		std::memcpy(dst, &value, sizeof(T));
	}

	template <typename T>
	void AppendPod(BufferSlice& buf, const T& value)
	{
		T* dst = AppendPod<T>(buf);
		std::memcpy(dst, &value, sizeof(T));
	}



	inline OwnedBufferSlice MakeOwned(const BufferSlice& slice)
	{
		return OwnedBufferSlice(slice);
	}

	inline OwnedBufferChain MakeOwnedChain(const BufferChain& chain)
	{
		OwnedBufferChain out;
		out.Reserve(chain.Parts().size());

		for (const BufferSlice& s : chain.Parts())
			out.Add(s);

		return out;
	}


	using Packet		= OwnedBufferSlice;
	using PacketView	= BufferSlice;
	using PacketChain	= OwnedBufferChain;



} // namespace jam::net
