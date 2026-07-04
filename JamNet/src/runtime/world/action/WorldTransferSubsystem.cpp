#include "pch.h"
#include "jamnet/runtime/world/action/WorldTransferSubsystem.h"
#include "jamnet/runtime/world/core/WorldBase.h"

namespace jam::net
{
	bool WorldTransferSubsystem::PrepareTransferOutDefault(ITransferOps& ops, const WorldTransferContext& ctx, std::shared_ptr<WorldTransferPayload>& payload, eWorldActionReason& failureReason)
	{
		if (!ctx.source.IsIssued())
		{
			failureReason = eWorldActionReason::InvalidArgument;
			return false;
		}

		bool invoked = false;
		const bool succeeded = ops.InvokeTransferHost(ctx.source, [&ctx, &payload](WorldMembershipHost& host)
			{
				payload = host.PrepareTransferOut(ctx);
				return true;
			}, invoked);
		failureReason = invoked && succeeded ? eWorldActionReason::None : eWorldActionReason::TargetUnavailable;
		return invoked && succeeded;
	}

	void WorldTransferSubsystem::CommitTransferOutDefault(ITransferOps& ops, const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload)
	{
		if (!ctx.source.IsIssued())
			return;

		bool invoked = false;
		ops.InvokeTransferHost(ctx.source, [&ctx, &payload](WorldMembershipHost& host)
			{
				host.CommitTransferOut(ctx, payload);
				return true;
			}, invoked);
	}

	void WorldTransferSubsystem::RollbackTransferOutDefault(ITransferOps& ops, const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload)
	{
		if (!ctx.source.IsIssued())
			return;

		bool invoked = false;
		ops.InvokeTransferHost(ctx.source, [&ctx, &payload](WorldMembershipHost& host)
			{
				host.RollbackTransferOut(ctx, payload);
				return true;
			}, invoked);
	}

	void WorldTransferSubsystem::CommitTransferInDefault(ITransferOps& ops, const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload)
	{
		if (!ctx.target.IsIssued())
			return;

		bool invoked = false;
		ops.InvokeTransferHost(ctx.target, [&ctx, &payload](WorldMembershipHost& host)
			{
				host.CommitTransferIn(ctx, payload);
				return true;
			}, invoked);
	}

	bool WorldTransferSubsystem::Execute(
		ITransferOps& ops,
		const WorldTransferContext& ctx,
		const WorldUserContext& user,
		eWorldActionReason& failureReason) const
	{
		if (!ctx.source.IsIssued() || !ctx.target.IsIssued())
		{
			failureReason = eWorldActionReason::InvalidArgument;
			return false;
		}

		if (ctx.source == ctx.target)
			return true;

		std::shared_ptr<WorldTransferPayload> payload;
		if (!ops.PrepareTransferOut(ctx, payload, failureReason))
			return false;

		if (!ops.PrepareTransferIn(ctx, payload, failureReason))
		{
			ops.RollbackTransferOut(ctx, payload);
			return false;
		}

		if (!ops.AddWorldMember(ctx, user, failureReason))
		{
			ops.RollbackTransferIn(ctx, payload);
			ops.RollbackTransferOut(ctx, payload);
			return false;
		}

		if (!ops.RemoveWorldMember(ctx, ctx.userId, failureReason))
		{
			ops.RollbackTransferIn(ctx, payload);
			ops.RollbackTransferOut(ctx, payload);
			return false;
		}

		ops.CommitTransferIn(ctx, payload);
		ops.CommitTransferOut(ctx, payload);
		return true;
	}
}
