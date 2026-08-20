#include "pch.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/PacketStructure.h"
#include "jamnet/core/utils/Clock.h"


namespace jam::net
{
	thread_local BufferPool::TlsFreeCaches		BufferPool::tl_freeCaches{};
	thread_local BufWriter::TlsCurrentBlocks	BufWriter::tl_currents{};

	BufWriter::TlsCurrentBlocks::~TlsCurrentBlocks()
	{
		BufWriter::FlushThreadLocal();
	}

	BufferPool::TlsFreeCaches::~TlsFreeCaches()
	{
		BufferPool::FlushThreadLocalCache();
	}


	BufferBlock::BufferBlock(BufferPool* owner, const uint32 capacity, const uint32 maxSliceStates)
		: m_owner(owner)
		, m_storage(std::make_unique<BYTE[]>(capacity))
		, m_capacity(capacity)
		, m_maxSliceStates(maxSliceStates)
		, m_sliceClosedStates(std::make_unique<std::atomic<uint8>[]>(maxSliceStates))
	{
		JAM_ASSERT(m_capacity > 0);
		JAM_ASSERT(m_maxSliceStates > 0);
	}

	void BufferBlock::AddRef()
	{
		m_refCount.fetch_add(1, std::memory_order_relaxed);
	}

	void BufferBlock::Release()
	{
		if (m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			JAM_ASSERT(m_owner);
			m_owner->Recycle(this);
		}
	}

	void BufferBlock::Reset()
	{
		m_used = 0;
		m_open = false;

		const uint32 usedStates = std::min<uint32>(m_nextSliceStateIndex.exchange(0, std::memory_order_relaxed), m_maxSliceStates);
		for (uint32 i = 0; i < usedStates; ++i)
			m_sliceClosedStates[i].store(0, std::memory_order_relaxed);
	}

	bool BufferBlock::TryReserve(uint32 bytes, uint32 alignment, OUT uint32& offset, OUT uint32& sliceStateIndex)
	{
		JAM_ASSERT(IsSupportedBufferAlignment(alignment));
		if (m_open) return false;

		const uint32 alignedOffset = static_cast<uint32>(AlignUp(static_cast<size_t>(m_used), static_cast<size_t>(alignment)));

		const uint32 newEnd = alignedOffset + bytes;
		if (newEnd > m_capacity)
			return false;

		m_open = true;
		offset = alignedOffset;
		sliceStateIndex = CreateSliceState(false);
		return true;
	}

	void BufferBlock::Commit(uint32 endOffset)
	{
		JAM_ASSERT(m_open);
		JAM_ASSERT(endOffset >= m_used);
		JAM_ASSERT(endOffset <= m_capacity);

		m_used = endOffset;
		m_open = false;
	}

	void BufferBlock::AbortReserve()
	{
		JAM_ASSERT(m_open);
		m_open = false;
	}

	uint32 BufferBlock::CreateSliceState(bool closed)
	{
		const uint32 index = m_nextSliceStateIndex.fetch_add(1, std::memory_order_relaxed);
		JAM_ASSERT(index < m_maxSliceStates);
		m_sliceClosedStates[index].store(closed ? 1 : 0, std::memory_order_release);
		return index;
	}

	bool BufferBlock::IsSliceClosed(uint32 sliceStateIndex) const
	{
		JAM_ASSERT(sliceStateIndex != k_invalidIndex);
		const uint32 stateCount = m_nextSliceStateIndex.load(std::memory_order_acquire);
		JAM_ASSERT(sliceStateIndex < stateCount);
		JAM_ASSERT(sliceStateIndex < m_maxSliceStates);
		return m_sliceClosedStates[sliceStateIndex].load(std::memory_order_acquire) != 0;
	}

	void BufferBlock::SetSliceClosed(uint32 sliceStateIndex, bool closed)
	{
		JAM_ASSERT(sliceStateIndex != k_invalidIndex);
		const uint32 stateCount = m_nextSliceStateIndex.load(std::memory_order_acquire);
		JAM_ASSERT(sliceStateIndex < stateCount);
		JAM_ASSERT(sliceStateIndex < m_maxSliceStates);
		m_sliceClosedStates[sliceStateIndex].store(closed ? 1 : 0, std::memory_order_release);
	}




	void BufferSlice::Validate() const
	{
		JAM_ASSERT(IsValid());
		JAM_ASSERT(begin <= head);
		JAM_ASSERT(head <= data);
		JAM_ASSERT(data <= tail);
		JAM_ASSERT(tail <= end);
		JAM_ASSERT(closedStateIndex != BufferBlock::k_invalidIndex);
	}

	void BufferSlice::ResetWindow(uint32 headroom)
	{
		JAM_ASSERT(IsValid());
		JAM_ASSERT(headroom <= Capacity());

		head = begin + headroom;
		data = head;
		tail = data;
	}

	bool BufferSlice::TryAppendPayload(uint32 bytes)
	{
		Validate();
		JAM_ASSERT(!IsClosed());

		if (Tailroom() < bytes)
			return false;

		tail += bytes;
		return true;
	}

	bool BufferSlice::TryPrependHeader(uint32 bytes)
	{
		Validate();
		JAM_ASSERT(!IsClosed());

		if (Headroom() < bytes)
			return false;

		head -= bytes;
		return true;
	}

	void BufferSlice::RemoveHeaderPrefix(uint32 bytes)
	{
		Validate();
		JAM_ASSERT(!IsClosed());
		JAM_ASSERT(bytes <= HeaderSize());

		head += bytes;
	}

	void BufferSlice::RemovePayloadPrefix(uint32 bytes)
	{
		Validate();
		JAM_ASSERT(!IsClosed());
		JAM_ASSERT(bytes <= PayloadSize());

		data += bytes;
		data = std::min(data, tail);
	}

	void BufferSlice::RemovePayloadSuffix(uint32 bytes)
	{
		Validate();
		JAM_ASSERT(!IsClosed());
		JAM_ASSERT(bytes <= PayloadSize());

		tail -= bytes;
	}

	void BufferSlice::RemoveSuffix(uint32 bytes)
	{
		Validate();
		JAM_ASSERT(bytes <= Size());

		tail -= bytes;
		data = std::min(data, tail);
		head = std::min(head, data);
	}

	void BufferSlice::CollapseHeader()
	{
		Validate();
		JAM_ASSERT(!IsClosed());
		data = head;
	}

	BufferSlice BufferSlice::SliceVisible(uint32 subOffset, uint32 subSize) const
	{
		Validate();
		JAM_ASSERT(subOffset + subSize <= Size());

		BufferSlice out;
		out.block  = block;
		out.begin  = head + subOffset;
		out.head   = out.begin;
		out.data   = out.begin;
		out.tail   = out.begin + subSize;
		out.end    = out.tail;
		out.closedStateIndex = IsClosed() ? closedStateIndex : block->CreateSliceState(true);
		return out;
	}

	BufferSlice BufferSlice::SlicePayload(uint32 subOffset, uint32 subSize) const
	{
		Validate();
		JAM_ASSERT(subOffset + subSize <= PayloadSize());

		BufferSlice out;
		out.block  = block;
		out.begin  = data + subOffset;
		out.head   = out.begin;
		out.data   = out.begin;
		out.tail   = out.begin + subSize;
		out.end    = out.tail;
		out.closedStateIndex = IsClosed() ? closedStateIndex : block->CreateSliceState(true);
		return out;
	}

	void BufferSlice::Close()
	{
		Validate();
		JAM_ASSERT(!IsClosed());

		block->Commit(tail); // absolute offset
		block->SetSliceClosed(closedStateIndex, true);
	}

	void BufferSlice::CloseWithCommit(uint32 committedBytes)
	{
		Validate();
		JAM_ASSERT(!IsClosed());
		JAM_ASSERT(committedBytes <= Capacity());

		block->Commit(begin + committedBytes); // absolute offset
		block->SetSliceClosed(closedStateIndex, true);
	}

	void BufferSlice::CommitReserved(uint32 committedBytes)
	{
		Validate();
		JAM_ASSERT(!IsClosed());
		JAM_ASSERT(committedBytes <= Capacity());

		block->Commit(begin + committedBytes); // absolute offset
	}

	void BufferSlice::Abort()
	{
		if (!IsValid() || IsClosed())
			return;

		block->AbortReserve();
		block->SetSliceClosed(closedStateIndex, true);
	}

	OwnedBufferSlice::OwnedBufferSlice(const BufferSlice& slice)
		: m_slice(slice)
	{
		AddRefIfNeeded();
	}

	OwnedBufferSlice::OwnedBufferSlice(BufferSlice&& slice)
		: m_slice(std::move(slice))
	{
		AddRefIfNeeded();
	}

	OwnedBufferSlice::OwnedBufferSlice(const OwnedBufferSlice& rhs)
		: m_slice(rhs.m_slice)
	{
		AddRefIfNeeded();
	}

	OwnedBufferSlice::OwnedBufferSlice(OwnedBufferSlice&& rhs) noexcept
		: m_slice(rhs.m_slice)
	{
		rhs.m_slice = {};
	}

	OwnedBufferSlice& OwnedBufferSlice::operator=(const OwnedBufferSlice& rhs)
	{
		if (this == &rhs)
			return *this;

		ReleaseIfNeeded();
		m_slice = rhs.m_slice;
		AddRefIfNeeded();
		return *this;
	}

	OwnedBufferSlice& OwnedBufferSlice::operator=(OwnedBufferSlice&& rhs) noexcept
	{
		if (this == &rhs)
			return *this;

		ReleaseIfNeeded();
		m_slice = rhs.m_slice;
		rhs.m_slice = {};
		return *this;
	}


	void OwnedBufferSlice::Reset()
	{
		ReleaseIfNeeded();
		m_slice = {};
	}


	void OwnedBufferSlice::AddRefIfNeeded()
	{
		if (m_slice.block)
			m_slice.block->AddRef();
	}

	void OwnedBufferSlice::ReleaseIfNeeded()
	{
		if (m_slice.block)
		{
			m_slice.block->Release();
			m_slice = {};
		}
	}




	BufferPool::BufferPool()
		: BufferPool(BufferPoolConfig{})
	{
	}

	BufferPool::BufferPool(BufferPoolConfig config)
		: m_config(config)
	{
		m_config.blockSize = std::max<uint32>(1, m_config.blockSize);
		m_config.maxSliceStates = std::max<uint32>(1, m_config.maxSliceStates);
		m_config.batchSize = std::max<uint32>(1, m_config.batchSize);
		m_config.trimBatchSize = std::max<uint32>(1, m_config.trimBatchSize);
		m_config.trimScanLimit = std::max(m_config.trimScanLimit, m_config.trimBatchSize);
		if (m_config.retainedBlockLimit != 0)
			m_config.retainedBlockLimit = std::max(m_config.retainedBlockLimit, m_config.initialBlocks);
		if (m_config.tlsHighWatermark == 0)
			m_config.tlsHighWatermark = m_config.batchSize;
		m_config.tlsLowWatermark = std::min(m_config.tlsLowWatermark, m_config.tlsHighWatermark);
		std::scoped_lock lock(m_lock);
		PreallocateLocked(m_config.initialBlocks);
	}

	BufferPool::~BufferPool()
	{
		std::scoped_lock lock(m_lock);

		JAM_ASSERT(m_liveBlockCount == m_freeIndices.size());
		for (BufferBlock* block : m_blocks)
			delete block;
	}

	BufferBlock* BufferPool::Acquire()
	{
		TlsFreeList& local = GetTlsFreeList();
		if (local.size() <= m_config.tlsLowWatermark)
			RefillLocalCache(local);

		if (!local.empty())
		{
			BufferBlock* block = local.back();
			local.pop_back();

			JAM_ASSERT(block != nullptr);
			JAM_ASSERT(block->m_owner == this);
			JAM_ASSERT(block->m_inFreeList == false);

			block->m_refCount.store(1, std::memory_order_relaxed);
			block->m_globalFreeSinceNs = 0;
			return block;
		}

		// Refill can fail only when allocation failed or config was hostile.
		JAM_ASSERT(false);
		return nullptr;
	}


	void BufferPool::Recycle(BufferBlock* block)
	{
		JAM_ASSERT(block != nullptr);
		JAM_ASSERT(block->m_owner == this);
		JAM_ASSERT(block->m_inFreeList == false);

		block->Reset();

		TlsFreeList& local = GetTlsFreeList();
		local.push_back(block);

		if (local.size() > m_config.tlsHighWatermark)
			SpillLocalCache(local, m_config.tlsLowWatermark);
	}

	void BufferPool::FlushThreadLocalCache()
	{
		for (auto& [pool, local] : tl_freeCaches.entries)
		{
			if (!pool)
			{
				local.clear();
				continue;
			}

			while (!local.empty())
				pool->SpillLocalCache(local, 0);
		}
		tl_freeCaches.entries.clear();
	}

	BufferPool::TlsFreeList& BufferPool::GetTlsFreeList()
	{
		return tl_freeCaches.entries[this];
	}

	BufferBlock* BufferPool::PopGlobalLocked()
	{
		if (!m_freeIndices.empty())
		{
			const uint32 idx = m_freeIndices.back();
			m_freeIndices.pop_back();

			JAM_ASSERT(idx < m_blocks.size());
			BufferBlock* block = m_blocks[idx];
			JAM_ASSERT(block != nullptr);
			JAM_ASSERT(block->m_inFreeList == true);

			block->m_inFreeList = false;
			block->m_globalFreeSinceNs = 0;
			return block;
		}

		return nullptr;
	}

	BufferBlock* BufferPool::AllocateBlockLocked(bool freeList)
	{
		BufferBlock* block = new BufferBlock(this, m_config.blockSize, m_config.maxSliceStates);
		uint32 poolIndex = 0;
		if (!m_vacantIndices.empty())
		{
			poolIndex = m_vacantIndices.back();
			m_vacantIndices.pop_back();
			JAM_ASSERT(poolIndex < m_blocks.size());
			JAM_ASSERT(m_blocks[poolIndex] == nullptr);
			m_blocks[poolIndex] = block;
		}
		else
		{
			poolIndex = static_cast<uint32>(m_blocks.size());
			m_blocks.push_back(block);
		}
		block->m_poolIndex  = poolIndex;
		block->m_inFreeList = freeList;
		block->m_refCount.store(0, std::memory_order_relaxed);
		block->Reset();
		block->m_globalFreeSinceNs = freeList ? NOW_NS() : 0;
		++m_liveBlockCount;

		if (freeList)
			m_freeIndices.push_back(block->m_poolIndex);

		return block;
	}

	void BufferPool::PreallocateLocked(uint32 count)
	{
		for (uint32 i = 0; i < count; ++i)
			AllocateBlockLocked(true);
	}

	void BufferPool::RefillLocalCache(TlsFreeList& local)
	{
		if (local.size() >= m_config.tlsHighWatermark)
			return;

		const uint32 target = std::min<uint32>(m_config.batchSize, static_cast<uint32>(m_config.tlsHighWatermark - local.size()));
		if (target == 0)
			return;

		std::unique_lock lock(m_lock);
		const uint64 globalLockSequence = ++m_globalLockSequence;

		uint32 moved = 0;
		for (; moved < target; ++moved)
		{
			BufferBlock* block = PopGlobalLocked();
			if (!block)
				block = AllocateBlockLocked(false);

			JAM_ASSERT(block != nullptr);
			JAM_ASSERT(block->m_refCount.load(std::memory_order_relaxed) == 0);
			JAM_ASSERT(block->m_inFreeList == false);
			local.push_back(block);
		}

		MaybeTrimGlobalLocked(NOW_NS(), globalLockSequence);
	}

	void BufferPool::SpillLocalCache(TlsFreeList& local, uint32 keepCount)
	{
		if (local.size() <= keepCount)
			return;

		const uint32 spillable = static_cast<uint32>(local.size() - keepCount);
		const uint32 target = std::min<uint32>(m_config.batchSize, spillable);
		if (target == 0)
			return;

		std::unique_lock lock(m_lock);
		const uint64 globalLockSequence = ++m_globalLockSequence;

		uint32 moved = 0;
		for (; moved < target; ++moved)
		{
			BufferBlock* block = local.back();
			local.pop_back();
			if (!block)
				continue;

			JAM_ASSERT(block->m_owner == this);
			JAM_ASSERT(block->m_poolIndex < m_blocks.size());
			JAM_ASSERT(m_blocks[block->m_poolIndex] == block);
			JAM_ASSERT(block->m_inFreeList == false);
			JAM_ASSERT(block->m_refCount.load(std::memory_order_relaxed) == 0);

			block->m_inFreeList = true;
			block->m_globalFreeSinceNs = NOW_NS();
			m_freeIndices.push_back(block->m_poolIndex);
		}

		MaybeTrimGlobalLocked(NOW_NS(), globalLockSequence);
	}

	void BufferPool::RecycleGlobal(BufferBlock* block)
	{
		JAM_ASSERT(block != nullptr);
		JAM_ASSERT(block->m_owner == this);
		JAM_ASSERT(block->m_inFreeList == false);
		JAM_ASSERT(block->m_refCount.load(std::memory_order_relaxed) == 0);

		std::unique_lock lock(m_lock);
		const uint64 globalLockSequence = ++m_globalLockSequence;

		JAM_ASSERT(block->m_poolIndex < m_blocks.size());
		JAM_ASSERT(m_blocks[block->m_poolIndex] == block);

		block->m_inFreeList = true;
		block->m_globalFreeSinceNs = NOW_NS();
		m_freeIndices.push_back(block->m_poolIndex);
		MaybeTrimGlobalLocked(block->m_globalFreeSinceNs, globalLockSequence);
	}

	void BufferPool::MaybeTrimGlobalLocked(const uint64 nowNs, const uint64 globalLockSequence)
	{
		if (m_config.retainedBlockLimit == 0
			|| m_config.trimEveryGlobalLocks == 0
			|| globalLockSequence % m_config.trimEveryGlobalLocks != 0
			|| m_liveBlockCount <= m_config.retainedBlockLimit)
		{
			return;
		}

		const uint64 excess = m_liveBlockCount - m_config.retainedBlockLimit;
		const uint32 target = static_cast<uint32>(std::min<uint64>(m_config.trimBatchSize, excess));
		uint32 trimmed = 0;
		uint32 scanned = 0;
		while (!m_freeIndices.empty() && trimmed < target && scanned < m_config.trimScanLimit)
		{
			if (m_trimScanCursor >= m_freeIndices.size())
				m_trimScanCursor = 0;

			const size_t candidatePos = m_trimScanCursor;
			const uint32 index = m_freeIndices[candidatePos];
			BufferBlock* block = index < m_blocks.size() ? m_blocks[index] : nullptr;
			++scanned;
			if (!block || block->m_globalFreeSinceNs == 0 || nowNs < block->m_globalFreeSinceNs
				|| nowNs - block->m_globalFreeSinceNs < m_config.trimMinAgeNs)
			{
				++m_trimScanCursor;
				continue;
			}

			JAM_ASSERT(block->m_inFreeList);
			JAM_ASSERT(block->m_refCount.load(std::memory_order_relaxed) == 0);
			m_freeIndices[candidatePos] = m_freeIndices.back();
			m_freeIndices.pop_back();
			m_blocks[index] = nullptr;
			m_vacantIndices.push_back(index);
			delete block;
			--m_liveBlockCount;
			++trimmed;
		}

	}

	BufferSlice BufWriter::Open(uint32 reserveBytes, uint32 initialHeadroom, uint32 alignment)
	{
		JAM_ASSERT(reserveBytes <= m_pool.BlockCapacity());
		JAM_ASSERT(initialHeadroom <= reserveBytes);
		JAM_ASSERT(IsSupportedBufferAlignment(alignment));

		BufferBlock*& cur = CurrentTLBlock();

		// outstanding I/O가 잡고 있으면 새 블록으로 로테이션
		if (cur && cur->IsOpen())
		{
			cur->Release();
			cur = nullptr;
		}

		if (cur == nullptr)
			cur = m_pool.Acquire();

		uint32 offset = 0;
		uint32 sliceStateIndex = BufferBlock::k_invalidIndex;
		if (!cur->TryReserve(reserveBytes, alignment, OUT offset, OUT sliceStateIndex))
		{
			cur->Release();
			cur = m_pool.Acquire();

			const bool ok = cur->TryReserve(reserveBytes, alignment, OUT offset, OUT sliceStateIndex);
			JAM_ASSERT(ok);
		}

		BufferSlice out;
		out.block  = cur;
		out.begin  = offset;
		out.head   = offset + initialHeadroom;
		out.data   = out.head;
		out.tail   = out.data;
		out.end    = offset + reserveBytes;
		out.closedStateIndex = sliceStateIndex;

		out.Validate();
		return out;
	}

	BufferSlice BufWriter::OpenForPacket(uint32 payloadSize, uint32 headerSize, uint32 alignment)
	{
		return Open(payloadSize + headerSize, headerSize, alignment);
	}

	BufferSlice BufWriter::OpenForPayload(uint32 payloadSize, uint32 alignment)
	{
		return Open(payloadSize, 0, alignment);
	}

	bool IoBufferReservation::Open(BufferPool& pool, const uint32 capacity, const uint32 alignment)
	{
		Reset();
		if (capacity == 0 || capacity > pool.BlockCapacity() || !IsSupportedBufferAlignment(alignment))
			return false;

		BufferBlock* block = pool.Acquire();
		if (!block)
			return false;

		uint32 offset = 0;
		uint32 sliceStateIndex = BufferBlock::k_invalidIndex;
		if (!block->TryReserve(capacity, alignment, OUT offset, OUT sliceStateIndex))
		{
			block->Release();
			return false;
		}

		BufferSlice slice;
		slice.block = block;
		slice.begin = offset;
		slice.head = offset;
		slice.data = offset;
		slice.tail = offset;
		slice.end = offset + capacity;
		slice.closedStateIndex = sliceStateIndex;
		slice.Validate();

		// Adopt Acquire()'s existing reference. The reservation is the only owner,
		// so opening an overlapped receive needs no extra atomic AddRef/Release pair.
		m_storage = OwnedBufferSlice(std::move(slice), OwnedBufferSlice::AdoptRefTag{});
		return true;
	}

	OwnedBufferSlice IoBufferReservation::Finalize(const uint32 committedBytes)
	{
		if (!IsValid() || committedBytes > Capacity())
		{
			Reset();
			return {};
		}

		BufferSlice& slice = m_storage.Get();
		if (!slice.TryAppendPayload(committedBytes))
		{
			Reset();
			return {};
		}

		slice.CloseWithCommit(committedBytes);
		return std::move(m_storage);
	}

	void IoBufferReservation::Reset()
	{
		if (!m_storage.IsValid())
			return;

		if (!m_storage->IsClosed())
			m_storage->Abort();
		m_storage.Reset();
	}

	uint32 BufferChain::TotalSize() const
	{
		uint32 sum = 0;
		for (const auto& p : m_parts)
			sum += p.Size();
		return sum;
	}

	OwnedBufferChain::OwnedBufferChain(const OwnedBufferChain& rhs)
	{
		m_parts.reserve(rhs.m_parts.size());
		for (const BufferSlice& s : rhs.m_parts)
			Add(s);
	}

	OwnedBufferChain::OwnedBufferChain(OwnedBufferChain&& rhs) noexcept
		: m_parts(std::move(rhs.m_parts))
	{
		rhs.m_parts.clear();
	}

	OwnedBufferChain& OwnedBufferChain::operator=(const OwnedBufferChain& rhs)
	{
		if (this == &rhs)
			return *this;

		Clear();
		m_parts.reserve(rhs.m_parts.size());
		for (const BufferSlice& s : rhs.m_parts)
			Add(s);

		return *this;
	}

	OwnedBufferChain& OwnedBufferChain::operator=(OwnedBufferChain&& rhs) noexcept
	{
		if (this == &rhs)
			return *this;

		Clear();
		m_parts = std::move(rhs.m_parts);
		rhs.m_parts.clear();
		return *this;
	}

	void OwnedBufferChain::Add(const BufferSlice& slice)
	{
		if (slice.block)
			slice.block->AddRef();

		m_parts.push_back(slice);
	}

	void OwnedBufferChain::Add(BufferSlice&& slice)
	{
		if (slice.block)
			slice.block->AddRef();

		m_parts.push_back(std::move(slice));
	}

	void OwnedBufferChain::Clear()
	{
		for (BufferSlice& s : m_parts)
		{
			if (s.block)
				s.block->Release();
		}
		m_parts.clear();
	}

	uint32 OwnedBufferChain::TotalSize() const
	{
		uint32 sum = 0;
		for (const auto& s : m_parts)
			sum += s.Size();
		return sum;
	}

	namespace
	{
		struct NetBufferPools
		{
			BufferPool packetSmall{ BufferPoolConfig{ .initialBlocks = 128, .tlsLowWatermark = 8, .tlsHighWatermark = 64, .batchSize = 16, .blockSize = 4 * 1024, .maxSliceStates = 1024, .retainedBlockLimit = 8192 } };
			BufferPool packetLarge{ BufferPoolConfig{ .initialBlocks = 128, .tlsLowWatermark = 8, .tlsHighWatermark = 64, .batchSize = 16, .blockSize = 8 * 1024, .maxSliceStates = 2048, .retainedBlockLimit = 4096 } };
			BufferPool tcpIo{ BufferPoolConfig{ .initialBlocks = 64, .tlsLowWatermark = 4, .tlsHighWatermark = 32, .batchSize = 8 } };
			BufferPool udpClientIo{ BufferPoolConfig{ .initialBlocks = 128, .tlsLowWatermark = 8, .tlsHighWatermark = 64, .batchSize = 16, .blockSize = JAMNET_MTU, .maxSliceStates = 1, .retainedBlockLimit = 4096 } };
			BufferPool udpServerIo{ BufferPoolConfig{ .initialBlocks = 512, .tlsLowWatermark = 32, .tlsHighWatermark = 128, .batchSize = 16, .blockSize = JAMNET_MTU, .maxSliceStates = 1, .retainedBlockLimit = 8192 }};
			BufferPool piggybackAck{ BufferPoolConfig{ .initialBlocks = 64, .tlsLowWatermark = 8, .tlsHighWatermark = 64, .batchSize = 16 } };
			BufferPool clone{ BufferPoolConfig{ .initialBlocks = 64, .tlsLowWatermark = 4, .tlsHighWatermark = 32, .batchSize = 8 } };
		};

		NetBufferPools& GetNetBufferPools()
		{
			static NetBufferPools pools;
			return pools;
		}
	}

	BufferPool& GetNetBufferPool(eNetBufferPoolKind kind)
	{
		NetBufferPools& pools = GetNetBufferPools();

		switch (kind)
		{
		case eNetBufferPoolKind::PacketSmall:	return pools.packetSmall;
		case eNetBufferPoolKind::PacketLarge:	return pools.packetLarge;
		case eNetBufferPoolKind::TcpIo:			return pools.tcpIo;
		case eNetBufferPoolKind::UdpClientIo:	return pools.udpClientIo;
		case eNetBufferPoolKind::UdpServerIo:	return pools.udpServerIo;
		case eNetBufferPoolKind::PiggybackAck:	return pools.piggybackAck;
		case eNetBufferPoolKind::Clone:			return pools.clone;
		default:
			JAM_ASSERT(false);
			return pools.packetSmall;
		}
	}

	BufferPool& GetPacketBufferPool(const uint32 packetBytes)
	{
		return GetNetBufferPool(packetBytes <= kPacketSmallMaxBytes ? eNetBufferPoolKind::PacketSmall : eNetBufferPoolKind::PacketLarge);
	}

	void FlushNetBufferThreadLocalCaches()
	{
		BufWriter::FlushThreadLocal();
		BufferPool::FlushThreadLocalCache();
	}

} // namespace jam::net
