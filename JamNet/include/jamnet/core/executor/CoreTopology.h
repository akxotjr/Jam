#pragma once

#include <cmath>

namespace jam
{
	struct CoreLayout
	{
		static constexpr int32 kFixedFiberWorkers = 1;

		int32  shards  = 1;
		int32  iocp    = 1;
		int32  fiber   = kFixedFiberWorkers;
		int32  offload = 1;

		bool IsValid() const
		{
			return shards >= 1 && iocp >= 1 && fiber == kFixedFiberWorkers && offload >= 1;
		}
	};

	enum eAutoCoreLayoutMode : uint8
	{
		IO_Heavy,
		CPU_Heavy,
		Balance
	};

	enum eCoreUsageProfile : uint8
	{
		CoreProfileServer,         // Maximize core usage without any room
		CoreProfileClient,         // 60~70% core usage with some room
		CoreProfileMultipleClient  // For running multiple client instances in a single process
	};

	struct AutoCoreLayoutConfig
	{
		eAutoCoreLayoutMode     mode             = Balance;
		bool                    isSmt            = true;
		uint32                  logicalCores     = std::thread::hardware_concurrency();      
		uint32                  physicalCores    = 0;
		uint32                  numaNodes        = 1;      
		uint32                  reservedThreads  = 0;
		eCoreUsageProfile       profile          = CoreProfileServer;
		float                   usageScale       = 0.f;
	};

	struct ExecutorAffinityConfig
	{
		bool                    useProfileDefaults = true;
		bool                    pinShardWorkers    = true;
		bool                    pinOffloadWorkers  = false;
		bool                    pinFiberWorker     = false;
		bool                    pinIocpWorkers     = false;
		bool                    pinMainThread      = false;
	};

	inline ExecutorAffinityConfig DefaultExecutorAffinityConfig(eCoreUsageProfile profile)
	{
		ExecutorAffinityConfig cfg{};
		cfg.useProfileDefaults  = false;
		cfg.pinShardWorkers     = true;
		cfg.pinIocpWorkers      = false;
		cfg.pinMainThread       = false;

		if (profile == CoreProfileServer)
		{
			cfg.pinOffloadWorkers = true;
			cfg.pinFiberWorker    = true;
		}
		else if (profile == CoreProfileMultipleClient)
		{
			// Multiple clients share a process and may run beside a pinned server.
			// Leave shard placement to the OS to avoid cross-process affinity overlap.
			cfg.pinShardWorkers   = false;
			cfg.pinOffloadWorkers = false;
			cfg.pinFiberWorker    = false;
		}
		else
		{
			cfg.pinOffloadWorkers = false;
			cfg.pinFiberWorker    = false;
		}

		return cfg;
	}

	inline CoreLayout AutoLayout(const AutoCoreLayoutConfig& cfg)
	{
		auto ceilDiv = [](uint32 a, uint32 b) -> uint32 { return (a + b - 1) / b; };

		const uint32 logical = cfg.logicalCores ? cfg.logicalCores : 1;
		uint32 phys = cfg.physicalCores;
		if (phys == 0)
			phys = cfg.isSmt ? std::max<uint32>(1u, logical / 2u) : logical;

		const uint32 reserved  = std::min<uint32>(phys, cfg.reservedThreads);
		const uint32 rawBudget = std::max<uint32>(1u, phys - reserved);

		float usageScale = cfg.usageScale;
		if (usageScale == 0.f)
		{
			usageScale = cfg.profile == CoreProfileClient ? (2.0f / 3.0f) : 1.0f;
		}

		uint32 budget = rawBudget;
		if (cfg.profile == CoreProfileClient || cfg.profile == CoreProfileMultipleClient)
		{
			const float scaled = std::max(1.0f, static_cast<float>(rawBudget) * usageScale);
			budget = static_cast<uint32>(std::ceil(scaled));
		}

		constexpr uint32 kMinRuntimeThreads = 4u;
		budget = std::max<uint32>(budget, kMinRuntimeThreads);

		CoreLayout layout{};
		layout.iocp  = 1;
		layout.fiber = CoreLayout::kFixedFiberWorkers;

		switch (cfg.mode)
		{
		case IO_Heavy:
		{
			layout.offload = static_cast<int32>(std::clamp<uint32>(ceilDiv(budget, 4), 1u, (budget > 3u) ? (budget - 3u) : 1u));
			break;
		}

		case CPU_Heavy:
		{
			layout.offload = 1;
			break;
		}

		case Balance:
		default:
		{
			layout.offload = static_cast<int32>(std::clamp<uint32>(ceilDiv(budget, 8), 1u, (budget > 3u) ? (budget - 3u) : 1u));
			break;
		}
		}

		const uint32 overhead = static_cast<uint32>(layout.iocp + layout.fiber + layout.offload);
		layout.shards = static_cast<int32>((budget > overhead) ? (budget - overhead) : 1u);

		auto total = [&]() -> uint32
		{
			return static_cast<uint32>(layout.shards + layout.iocp + layout.fiber + layout.offload);
		};

		const uint32 logicalCap   = (logical > reserved) ? (logical - reserved) : 1u;
		const uint32 effectiveCap = std::max<uint32>(logicalCap, kMinRuntimeThreads);
		if (total() > effectiveCap)
		{
			uint32 extra = total() - effectiveCap;
			auto trim = [&](int32& value, int32 minValue)
				{
					if (extra == 0 || value <= minValue)
						return;

					const uint32 removable = static_cast<uint32>(value - minValue);
					const uint32 cut = std::min<uint32>(removable, extra);
					value -= static_cast<int32>(cut);
					extra -= cut;
				};

			trim(layout.offload, 1);
			trim(layout.shards, 1);
			if (layout.shards == 0) layout.shards = 1;
		}

		const uint32 nodes = cfg.numaNodes ? cfg.numaNodes : 1u;
		if (nodes > 1 && static_cast<uint32>(layout.shards) >= nodes)
		{
			uint32 rem = static_cast<uint32>(layout.shards) % nodes;
			if (rem != 0)
			{
				uint32 canGrow = (total() < effectiveCap) ? (effectiveCap - total()) : 0u;
				uint32 add = nodes - rem;
				if (add <= canGrow)
				{
					layout.shards += static_cast<int32>(add);
				}
				else
				{
					layout.shards -= static_cast<int32>(rem);
					if (layout.shards == 0) layout.shards = static_cast<int32>(nodes);
				}
			}
		}

		return layout;
	}
}
