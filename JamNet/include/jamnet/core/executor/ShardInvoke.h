#pragma once

#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"

#include <future>
#include <optional>
#include <type_traits>
#include <utility>

namespace jam
{
	namespace detail
	{
		template <typename T>
		struct ShardInvokeState
		{
			std::optional<T> result;
		};

		template <>
		struct ShardInvokeState<void>
		{
		};
	}

	template <typename Fn>
	using ShardInvokeResult = std::decay_t<std::invoke_result_t<std::decay_t<Fn>, ShardLocal&>>;

	template <typename Fn>
	ShardInvokeResult<Fn> InvokeOnShard(const std::shared_ptr<ShardExecutor>& shard, Fn&& fn, eJobPriority priority = eJobPriority::Control)
	{
		using Result = ShardInvokeResult<Fn>;
		using FnT = std::decay_t<Fn>;

		if (!shard)
		{
			if constexpr (!std::is_void_v<Result>)
				return Result{};
			else
				return;
		}

		const uint32 targetShardIndex = static_cast<uint32>(shard->GetIndex());
		if (auto* local = CurrentShardLocal(); local && local->shardIndex == targetShardIndex)
		{
			if constexpr (std::is_void_v<Result>)
			{
				std::forward<Fn>(fn)(*local);
				return;
			}
			else
			{
				return std::forward<Fn>(fn)(*local);
			}
		}

		if (auto* local = CurrentShardLocal(); local && local->scheduler && local->scheduler->Current() != 0)
		{
			if (auto* callerExecutor = static_cast<ShardExecutor*>(CurrentExecutor()))
			{
				auto state = std::make_shared<detail::ShardInvokeState<Result>>();
				const FiberAwaitKey awaitKey = callerExecutor->AllocateAwaitKey();

				shard->Submit(Job(
					[state, fn = FnT(std::forward<Fn>(fn)), callerExecutor, awaitKey]() mutable
					{
						if constexpr (std::is_void_v<Result>)
						{
							fn(CurrentShardLocalChecked());
						}
						else
						{
							state->result.emplace(fn(CurrentShardLocalChecked()));
						}

						callerExecutor->ResumeFiber(awaitKey);
					},
					priority));

				const bool resumed = local->scheduler->Suspend(awaitKey, 0);
				JAM_ASSERT(resumed);

				if constexpr (std::is_void_v<Result>)
				{
					return;
				}
				else
				{
					JAM_ASSERT(state->result.has_value());
					return std::move(*state->result);
				}
			}
		}

		std::promise<Result> done;
		auto future = done.get_future();

		shard->Submit(Job(
			[fn = FnT(std::forward<Fn>(fn)), done = std::move(done)]() mutable
			{
				if constexpr (std::is_void_v<Result>)
				{
					fn(CurrentShardLocalChecked());
					done.set_value();
				}
				else
				{
					done.set_value(fn(CurrentShardLocalChecked()));
				}
			},
			priority));

		if constexpr (std::is_void_v<Result>)
		{
			future.wait();
			return;
		}
		else
		{
			return future.get();
		}
	}

	inline void InvokeOnShard(const std::shared_ptr<ShardExecutor>& shard, Job job)
	{
		if (!shard)
			return;

		const uint32 targetShardIndex = static_cast<uint32>(shard->GetIndex());
		if (auto* local = CurrentShardLocal(); local && local->shardIndex == targetShardIndex)
		{
			job.Execute();
			return;
		}

		const eJobPriority priority = job.Priority();
		if (auto* local = CurrentShardLocal(); local && local->scheduler && local->scheduler->Current() != 0)
		{
			if (auto* callerExecutor = static_cast<ShardExecutor*>(CurrentExecutor()))
			{
				const FiberAwaitKey awaitKey = callerExecutor->AllocateAwaitKey();
				shard->Submit(Job(
					[job = std::move(job), callerExecutor, awaitKey]() mutable
					{
						job.Execute();
						callerExecutor->ResumeFiber(awaitKey);
					},
					priority));

				const bool resumed = local->scheduler->Suspend(awaitKey, 0);
				JAM_ASSERT(resumed);
				return;
			}
		}

		std::promise<void> done;
		auto future = done.get_future();

		shard->Submit(Job(
			[job = std::move(job), done = std::move(done)]() mutable
			{
				job.Execute();
				done.set_value();
			},
			priority));
		future.wait();
	}
}
