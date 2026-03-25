#pragma once
#include "ReplicationTypes.h"


namespace jam::net
{
	// ---- common ---- 

	struct TickCounter
	{
		uint32 tick = 0;

		void Init() { tick = 0; }
		void Tick() { ++tick; }
	};

	// ---- client ----

	struct EstimatedServerTick
	{
		bool	valid				= false;

		uint64	anchorServerTick	= 0;
		uint64	anchorRecvNs		= 0;

		uint64	lastSnapshotTick	= 0;
		uint64	lastSnapshotRecvNs	= 0;

		double	estimatedNowTick	= 0.0;

		void Reset()
		{
			valid				= false;
			anchorServerTick	= 0;
			anchorRecvNs		= 0;
			lastSnapshotTick	= 0;
			lastSnapshotRecvNs	= 0;
			estimatedNowTick	= 0.0;
		}

		void Update(uint64 snapshotServerTick, uint64 snapshotRecvNs, uint64 nowNs)
		{
			// out-of-order snapshot 보호: 더 최신 tick만 anchor 갱신
			if (!valid || snapshotServerTick >= anchorServerTick)
			{
				anchorServerTick = snapshotServerTick;
				anchorRecvNs	 = snapshotRecvNs;
				valid = true;
			}

			lastSnapshotTick   = snapshotServerTick;
			lastSnapshotRecvNs = snapshotRecvNs;

			const uint64 elapsedNs = (nowNs > anchorRecvNs) ? (nowNs - anchorRecvNs) : 0;
			estimatedNowTick = static_cast<double>(anchorServerTick) + (static_cast<double>(elapsedNs) / static_cast<double>(SIMULATION_TICK_NS));
		}
	};




	struct ReconcileSignal
	{
		uint64		serverTick	= 0;
		uint32		inputAck	= 0;
		bool		dirty		= false;
	};

	struct InputHistoryBuffer
	{
		InputCmd	current		= {};
		uint32		maxSamples	= 256;

		InputHistoryBuffer()
		{
			Init(maxSamples);
		}

		void Init(uint32 capacity)
		{
			maxSamples = (capacity == 0) ? 1u : capacity;
			samples.resize(maxSamples);
			head    = 0;
			count   = 0;
			current = {};
		}

		void Clear()
		{
			head    = 0;
			count   = 0;
			current = {};
		}

		void Push(const InputCmd& cmd)
		{
			if (samples.empty())
				Init(maxSamples);

			const uint32 idx = (head + count) % maxSamples;
			samples[idx] = cmd;

			if (count < maxSamples)
			{
				++count;
			}
			else
			{
				head = (head + 1) % maxSamples;
			}
		}

		void PruneAck(uint32 ackSeq)
		{
			while (count > 0)
			{
				const InputCmd& front = samples[head];
				if (front.seq > ackSeq)
					break;

				head = (head + 1) % maxSamples;
				--count;
			}
		}

		template <typename Fn>
		void ForEachReplayRange(uint32 ackSeqExclusive, uint32 currentSeqExclusive, Fn&& fn) const
		{
			for (uint32 i = 0; i < count; ++i)
			{
				const InputCmd& cmd = samples[(head + i) % maxSamples];
				if (cmd.seq <= ackSeqExclusive)
					continue;
				if (cmd.seq >= currentSeqExclusive)
					break;

				fn(cmd);
			}
		}

	private:
		std::vector<InputCmd>	samples;
		uint32					head	= 0;
		uint32					count	= 0;
	};

	struct PredictedStateSample
	{
		uint32					inputSeq = 0;
		px::CharacterState		state	 = {};
	};


	struct PredictedHistoryBuffer
	{
		uint32 maxSamples = 256;
		PredictedHistoryBuffer() { Init(maxSamples); }

		void Init(uint32 capacity)
		{
			maxSamples = (capacity == 0) ? 1u : capacity;
			samples.resize(maxSamples);
			head = 0; count = 0;
		}

		void Push(uint32 inputSeq, const px::CharacterState& state)
		{
			const uint32 idx = (head + count) % maxSamples;
			samples[idx] = PredictedStateSample{ .inputSeq = inputSeq, .state = state };
			if (count < maxSamples) ++count;
			else head = (head + 1) % maxSamples;
		}

		void PruneAck(uint32 ackSeq)
		{
			while (count > 0 && samples[head].inputSeq <= ackSeq)
			{
				head = (head + 1) % maxSamples;
				--count;
			}
		}

		// ack+1 이후 구간을 replay commit으로 교체하기 위해 사용
		void PruneFrom(uint32 fromSeqInclusive)
		{
			std::vector<PredictedStateSample> kept(maxSamples);
			uint32 keptCount = 0;

			for (uint32 i = 0; i < count; ++i)
			{
				const PredictedStateSample& s = samples[(head + i) % maxSamples];
				if (s.inputSeq < fromSeqInclusive)
					kept[keptCount++] = s;
			}

			samples.swap(kept);
			head  = 0;
			count = keptCount;
		}

	private:
		std::vector<PredictedStateSample> samples;
		uint32 head  = 0;
		uint32 count = 0;
	};

	// replay 중간 결과 임시 버퍼 (commit 전용)
	struct ReplayPredictedBuffer
	{
		std::vector<PredictedStateSample> samples;

		void Clear() { samples.clear(); }

		void Push(uint32 inputSeq, const px::CharacterState& state)
		{
			samples.push_back(PredictedStateSample{ .inputSeq = inputSeq, .state = state });
		}
	};

	struct RenderCorrectionDelta
	{
		px::Vec3 pos = px::Vec3::Zero();
		float yaw = 0.0f;
		float pitch = 0.0f;
	};

	// current input -> main scene -> local char state
	using LivePredictedState  = px::CharacterState;

	// resim [ack+1 ~ current] -> replay scene -> local char state
	using CorrectionState     = px::CharacterState;


	// ---- server ----







	// ---- helpers ----

	static void InitClientNetWorldCtx(entt::registry& world)
	{
		world.ctx().emplace<TickCounter>();

		world.ctx().emplace<EstimatedServerTick>();
		world.ctx().emplace<ReconcileSignal>();
		world.ctx().emplace<InputHistoryBuffer>();
		world.ctx().emplace<PredictedHistoryBuffer>();
		world.ctx().emplace<ReplayPredictedBuffer>();
		world.ctx().emplace<LivePredictedState>();
		world.ctx().emplace<CorrectionState>();
		world.ctx().emplace<RenderCorrectionDelta>();
	}

	static void InitServerNetWorldCtx(entt::registry& world)
	{
		
	}
}
