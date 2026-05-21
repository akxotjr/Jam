#pragma once
#include "jamnet/runtime/world/WorldActionTypes.h"

#include <functional>
#include <memory>

namespace jam::net
{
	struct WorldUserContext;
	class WorldMembershipHost;

	class WorldTransferSubsystem
	{
	public:
		struct ITransferOps
		{
			virtual ~ITransferOps() = default;

			virtual bool	PrepareTransferOut(const WorldTransferContext& ctx, std::shared_ptr<WorldTransferPayload>& payload, eWorldActionReason& failureReason) = 0;
			virtual bool	PrepareTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload, eWorldActionReason& failureReason) = 0;
			virtual void	CommitTransferOut(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) = 0;
			virtual void	RollbackTransferOut(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) = 0;
			virtual void	CommitTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) = 0;
			virtual void	RollbackTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) = 0;

			virtual bool	AddWorldMember(const WorldTransferContext& ctx, WorldUserContext user, eWorldActionReason& failureReason) = 0;
			virtual bool	RemoveWorldMember(const WorldTransferContext& ctx, uint64 userId, eWorldActionReason& failureReason) = 0;
			
			virtual bool	InvokeTransferHost(const WorldKey& key, std::function<bool(WorldMembershipHost&)> fn, bool& invoked) = 0;
		};

		static bool PrepareTransferOutDefault(ITransferOps& ops, const WorldTransferContext& ctx, std::shared_ptr<WorldTransferPayload>& payload, eWorldActionReason& failureReason);
		static void CommitTransferOutDefault(ITransferOps& ops, const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload);
		static void RollbackTransferOutDefault(ITransferOps& ops, const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload);
		static void CommitTransferInDefault(ITransferOps& ops, const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload);

		bool Execute(
			ITransferOps& ops,
			const WorldTransferContext& ctx,
			const WorldUserContext& user,
			eWorldActionReason& failureReason) const;
	};
}
