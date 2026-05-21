#include "pch.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/utils/Clock.h"


namespace jam::net
{
	thread_local BufferPool::TlsFreeCaches	BufferPool::tl_freeCaches{};
	thread_local BufWriter::TlsCurrentBlocks	BufWriter::tl_currents{};
	thread_local uint32						g_bufWriterOpenDepth = 0;

	BufWriter::TlsCurrentBlocks::~TlsCurrentBlocks()
	{
		BufWriter::FlushThreadLocal();
	}

	BufferPool::TlsFreeCaches::~TlsFreeCaches()
	{
		BufferPool::FlushThreadLocalCache();
	}


	BufferBlock::BufferBlock(BufferPool* owner)
		: m_owner(owner)
	{
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

		const uint32 usedStates = std::min<uint32>(m_nextSliceStateIndex.exchange(0, std::memory_order_relaxed), k_maxSliceStates);
		for (uint32 i = 0; i < usedStates; ++i)
			m_sliceClosedStates[i].store(0, std::memory_order_relaxed);
	}

	bool BufferBlock::TryReserve(uint32 bytes, uint32 alignment, OUT uint32& offset, OUT uint32& sliceStateIndex)
	{
		JAM_ASSERT(IsSupportedBufferAlignment(alignment));
		if (m_open) return false;

		const uint32 alignedOffset = static_cast<uint32>(AlignUp(static_cast<size_t>(m_used), static_cast<size_t>(alignment)));

		const uint32 newEnd = alignedOffset + bytes;
		if (newEnd > k_blockSize)
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
		JAM_ASSERT(endOffset <= k_blockSize);

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
		JAM_ASSERT(index < k_maxSliceStates);
		m_sliceClosedStates[index].store(closed ? 1 : 0, std::memory_order_release);
		return index;
	}

	bool BufferBlock::IsSliceClosed(uint32 sliceStateIndex) const
	{
		JAM_ASSERT(sliceStateIndex != k_invalidIndex);
		const uint32 stateCount = m_nextSliceStateIndex.load(std::memory_order_acquire);
		JAM_ASSERT(sliceStateIndex < stateCount);
		JAM_ASSERT(sliceStateIndex < k_maxSliceStates);
		return m_sliceClosedStates[sliceStateIndex].load(std::memory_order_acquire) != 0;
	}

	void BufferBlock::SetSliceClosed(uint32 sliceStateIndex, bool closed)
	{
		JAM_ASSERT(sliceStateIndex != k_invalidIndex);
		const uint32 stateCount = m_nextSliceStateIndex.load(std::memory_order_acquire);
		JAM_ASSERT(sliceStateIndex < stateCount);
		JAM_ASSERT(sliceStateIndex < k_maxSliceStates);
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
		out.closedStateIndex = block->CreateSliceState(true);
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
		out.closedStateIndex = block->CreateSliceState(true);
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
		m_config.batchSize = std::max<uint32>(1, m_config.batchSize);
		if (m_config.tlsHighWatermark == 0)
			m_config.tlsHighWatermark = m_config.batchSize;
		m_config.tlsLowWatermark = std::min(m_config.tlsLowWatermark, m_config.tlsHighWatermark);
		if (m_config.debugName == nullptr)
			m_config.debugName = "BufferPool";

		std::scoped_lock lock(m_lock);
		PreallocateLocked(m_config.initialBlocks);
	}

	BufferPool::~BufferPool()
	{
		std::scoped_lock lock(m_lock);

		JAM_ASSERT(m_blocks.size() == m_freeIndices.size());
		for (BufferBlock* block : m_blocks)
			delete block;
	}

	BufferBlock* BufferPool::Acquire()
	{
		m_metrics.acquireCalls.fetch_add(1, std::memory_order_relaxed);

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
			return block;
		}

		// Refill can fail only when allocation failed or config was hostile.
		JAM_ASSERT(false);
		return nullptr;
	}


	void BufferPool::Recycle(BufferBlock* block)
	{
		m_metrics.recycleCalls.fetch_add(1, std::memory_order_relaxed);

		JAM_ASSERT(block != nullptr);
		JAM_ASSERT(block->m_owner == this);
		JAM_ASSERT(block->m_inFreeList == false);

		block->Reset();

		TlsFreeList& local = GetTlsFreeList();
		local.push_back(block);
		UpdatePeakTlsFree(local.size());

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
			return block;
		}

		return nullptr;
	}

	BufferBlock* BufferPool::AllocateBlockLocked(bool freeList)
	{
		BufferBlock* block = new BufferBlock(this);
		block->m_poolIndex  = static_cast<uint32>(m_blocks.size());
		block->m_inFreeList = freeList;
		block->m_refCount.store(0, std::memory_order_relaxed);
		block->Reset();
		m_blocks.push_back(block);

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

		const uint64 lockStart = NOW_NS();
		std::unique_lock lock(m_lock);
		const uint64 waitNs = NOW_NS() - lockStart;

		m_metrics.acquireLockWait_ns.fetch_add(waitNs, std::memory_order_relaxed);
		const uint64 globalLocks = m_metrics.globalLockCount.fetch_add(1, std::memory_order_relaxed) + 1;
		m_metrics.refillCount.fetch_add(1, std::memory_order_relaxed);

		uint32 moved = 0;
		for (; moved < target; ++moved)
		{
			BufferBlock* block = PopGlobalLocked();
			if (!block)
			{
				block = AllocateBlockLocked(false);
				m_metrics.slowAllocCount.fetch_add(1, std::memory_order_relaxed);
			}

			JAM_ASSERT(block != nullptr);
			JAM_ASSERT(block->m_refCount.load(std::memory_order_relaxed) == 0);
			JAM_ASSERT(block->m_inFreeList == false);
			local.push_back(block);
		}

		m_metrics.refillBlockCount.fetch_add(moved, std::memory_order_relaxed);
		UpdatePeakTlsFree(local.size());
		lock.unlock();

		//LogMetricsIfNeeded(globalLocks);
	}

	void BufferPool::SpillLocalCache(TlsFreeList& local, uint32 keepCount)
	{
		if (local.size() <= keepCount)
			return;

		const uint32 spillable = static_cast<uint32>(local.size() - keepCount);
		const uint32 target = std::min<uint32>(m_config.batchSize, spillable);
		if (target == 0)
			return;

		const uint64 lockStart = NOW_NS();
		std::unique_lock lock(m_lock);
		const uint64 waitNs = NOW_NS() - lockStart;

		m_metrics.recycleLockWait_ns.fetch_add(waitNs, std::memory_order_relaxed);
		const uint64 globalLocks = m_metrics.globalLockCount.fetch_add(1, std::memory_order_relaxed) + 1;
		m_metrics.spillCount.fetch_add(1, std::memory_order_relaxed);

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
			m_freeIndices.push_back(block->m_poolIndex);
		}

		m_metrics.spillBlockCount.fetch_add(moved, std::memory_order_relaxed);
		lock.unlock();

		//LogMetricsIfNeeded(globalLocks);
	}

	void BufferPool::RecycleGlobal(BufferBlock* block)
	{
		JAM_ASSERT(block != nullptr);
		JAM_ASSERT(block->m_owner == this);
		JAM_ASSERT(block->m_inFreeList == false);
		JAM_ASSERT(block->m_refCount.load(std::memory_order_relaxed) == 0);

		const uint64 lockStart = NOW_NS();
		std::unique_lock lock(m_lock);
		const uint64 waitNs = NOW_NS() - lockStart;

		m_metrics.recycleLockWait_ns.fetch_add(waitNs, std::memory_order_relaxed);
		const uint64 globalLocks = m_metrics.globalLockCount.fetch_add(1, std::memory_order_relaxed) + 1;
		m_metrics.directRecycleCount.fetch_add(1, std::memory_order_relaxed);

		JAM_ASSERT(block->m_poolIndex < m_blocks.size());
		JAM_ASSERT(m_blocks[block->m_poolIndex] == block);

		block->m_inFreeList = true;
		m_freeIndices.push_back(block->m_poolIndex);
		lock.unlock();

		//LogMetricsIfNeeded(globalLocks);
	}

	void BufferPool::UpdatePeakTlsFree(size_t count)
	{
		uint64 current = m_metrics.peakTlsFreeCount.load(std::memory_order_relaxed);
		while (count > current && !m_metrics.peakTlsFreeCount.compare_exchange_weak(
			current,
			static_cast<uint64>(count),
			std::memory_order_relaxed,
			std::memory_order_relaxed))
		{
		}
	}

	void BufferPool::LogMetricsIfNeeded(uint64 globalLockCount)
	{
		const uint64 every = m_config.logEveryGlobalLocks;
		if (every == 0 || globalLockCount % every != 0)
			return;

		const uint64 refillCount = m_metrics.refillCount.load(std::memory_order_relaxed);
		const uint64 refillBlocks = m_metrics.refillBlockCount.load(std::memory_order_relaxed);
		const uint64 spillCount = m_metrics.spillCount.load(std::memory_order_relaxed);
		const uint64 spillBlocks = m_metrics.spillBlockCount.load(std::memory_order_relaxed);
		const uint64 directRecycleCount = m_metrics.directRecycleCount.load(std::memory_order_relaxed);
		const uint64 acquireWait = m_metrics.acquireLockWait_ns.load(std::memory_order_relaxed);
		const uint64 recycleWait = m_metrics.recycleLockWait_ns.load(std::memory_order_relaxed);
		const uint64 recycleLockOps = spillCount + directRecycleCount;

		JAMNET_LOG_INFO(
			"[BufferPool:{}] globalLocks={} acquire={} recycle={} slowAlloc={} refill={}/{} spill={}/{} directRecycle={} peakTls={} avgAcquireLockWait={}ns avgRecycleLockWait={}ns batch={} low={} high={}",
			m_config.debugName,
			globalLockCount,
			m_metrics.acquireCalls.load(std::memory_order_relaxed),
			m_metrics.recycleCalls.load(std::memory_order_relaxed),
			m_metrics.slowAllocCount.load(std::memory_order_relaxed),
			refillCount,
			refillBlocks,
			spillCount,
			spillBlocks,
			directRecycleCount,
			m_metrics.peakTlsFreeCount.load(std::memory_order_relaxed),
			refillCount ? (acquireWait / refillCount) : 0,
			recycleLockOps ? (recycleWait / recycleLockOps) : 0,
			m_config.batchSize,
			m_config.tlsLowWatermark,
			m_config.tlsHighWatermark);
	}


	BufferSlice BufWriter::Open(uint32 reserveBytes, uint32 initialHeadroom, uint32 alignment)
	{
		JAM_ASSERT(reserveBytes <= BufferBlock::k_blockSize);
		JAM_ASSERT(initialHeadroom <= reserveBytes);
		JAM_ASSERT(IsSupportedBufferAlignment(alignment));

		struct OpenDepthScope
		{
			uint32& depthRef;
			explicit OpenDepthScope(uint32& depth) : depthRef(depth)
			{
				++depthRef;
			}
			~OpenDepthScope()
			{
				JAM_ASSERT(depthRef > 0);
				--depthRef;
			}
		} depthScope(g_bufWriterOpenDepth);

		if (g_bufWriterOpenDepth > 1)
		{
			JAMNET_LOG_WARN(
				"[BufWriter::Open] nested open detected. pool={}, depth={}, reserveBytes={}, initialHeadroom={}, alignment={}",
				m_pool.m_config.debugName ? m_pool.m_config.debugName : "BufferPool",
				g_bufWriterOpenDepth,
				reserveBytes,
				initialHeadroom,
				alignment);
		}

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
			BufferPool packet{ BufferPoolConfig{ .initialBlocks = 128, .tlsLowWatermark = 8, .tlsHighWatermark = 64, .batchSize = 16, .logEveryGlobalLocks = 1024, .debugName = "Packet" } };
			BufferPool tcpIo{ BufferPoolConfig{ .initialBlocks = 64, .tlsLowWatermark = 4, .tlsHighWatermark = 32, .batchSize = 8, .logEveryGlobalLocks = 1024, .debugName = "TcpIo" } };
			BufferPool udpClientIo{ BufferPoolConfig{ .initialBlocks = 128, .tlsLowWatermark = 8, .tlsHighWatermark = 64, .batchSize = 16, .logEveryGlobalLocks = 1024, .debugName = "UdpIo" } };
			BufferPool udpServerIo{ BufferPoolConfig{ .initialBlocks = 512, .tlsLowWatermark = 32, .tlsHighWatermark = 128, .batchSize = 16, .logEveryGlobalLocks = 1024, .debugName = "UdpServerIo" }};
			BufferPool piggybackAck{ BufferPoolConfig{ .initialBlocks = 64, .tlsLowWatermark = 8, .tlsHighWatermark = 64, .batchSize = 16, .logEveryGlobalLocks = 1024, .debugName = "PiggybackAck" } };
			BufferPool clone{ BufferPoolConfig{ .initialBlocks = 64, .tlsLowWatermark = 4, .tlsHighWatermark = 32, .batchSize = 8, .logEveryGlobalLocks = 1024, .debugName = "Clone" } };
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
		case eNetBufferPoolKind::Packet:		return pools.packet;
		case eNetBufferPoolKind::TcpIo:			return pools.tcpIo;
		case eNetBufferPoolKind::UdpClientIo:	return pools.udpClientIo;
		case eNetBufferPoolKind::UdpServerIo:	return pools.udpServerIo;
		case eNetBufferPoolKind::PiggybackAck:	return pools.piggybackAck;
		case eNetBufferPoolKind::Clone:			return pools.clone;
		default:
			JAM_ASSERT(false);
			return pools.packet;
		}
	}

	void FlushNetBufferThreadLocalCaches()
	{
		BufWriter::FlushThreadLocal();
		BufferPool::FlushThreadLocalCache();
	}

} // namespace jam::net
