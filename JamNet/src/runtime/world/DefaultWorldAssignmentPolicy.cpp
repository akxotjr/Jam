#include "pch.h"
#include "jamnet/runtime/world/DefaultWorldAssignmentPolicy.h"

#include "jamnet/runtime/world/WorldDirectory.h"

#include <algorithm>

namespace jam::net
{
	namespace
	{
		const WorldMembership* FindMembershipByDesc(const UserMembershipSnapshotEntry& memberships, uint32 descId)
		{
			auto it = std::ranges::find_if(memberships.memberships, [descId](const WorldMembership& membership)
				{
					return membership.key.descId == descId;
				});
			return (it != memberships.memberships.end()) ? &(*it) : nullptr;
		}

		const WorldMembership* FindMembershipByKey(const UserMembershipSnapshotEntry& memberships, const WorldKey& key)
		{
			auto it = std::ranges::find_if(memberships.memberships, [&key](const WorldMembership& membership)
				{
					return membership.key == key;
				});
			return (it != memberships.memberships.end()) ? &(*it) : nullptr;
		}

		bool IsJoinableRuntimeState(const WorldMeta& entry)
		{
			if (entry.kind != eWorldKind::Physical)
				return true;

			return entry.runtime == ePhysicalWorldRuntimeState::Standby
				|| entry.runtime == ePhysicalWorldRuntimeState::Active
				|| entry.runtime == ePhysicalWorldRuntimeState::Paused;
		}

		const WorldMembership* FindActivePhysicalMembership(const UserMembershipSnapshotEntry& memberships)
		{
			auto it = std::ranges::find_if(memberships.memberships, [](const WorldMembership& membership)
				{
					return membership.presence == eWorldMembershipPresence::Active;
				});
			return (it != memberships.memberships.end()) ? &(*it) : nullptr;
		}
	}

	uint32 DefaultWorldAssignmentPolicy::ResolveDescId(const WorldActionRequest& req) const
	{
		if (req.target.descId != 0)
			return req.target.descId;
		if (req.source.descId != 0)
			return req.source.descId;
		return 0;
	}

	const WorldDesc* DefaultWorldAssignmentPolicy::ResolveDesc(const WorldActionRequest& req) const
	{
		const uint32 templateId = ResolveDescId(req);
		if (templateId == 0 || !m_asset)
			return nullptr;
		return m_asset->Find(templateId);
	}

	std::optional<WorldMeta> DefaultWorldAssignmentPolicy::SelectWorld(
		const WorldDesc& desc,
		std::span<const WorldMeta> candidates) const
	{
		(void)desc;
		const WorldMeta* selected = nullptr;
		for (const auto& candidate : candidates)
		{
			if (!candidate.IsValid())
				continue;

			if (!IsJoinableRuntimeState(candidate))
				continue;

			if (!candidate.HasCapacity())
				continue;

			if (!selected || candidate.memberCount < selected->memberCount)
				selected = &candidate;
		}

		return selected ? std::optional(*selected) : std::nullopt;
	}

	WorldActionPlan DefaultWorldAssignmentPolicy::PlanAction(const WorldActionRequest& req)
	{
		if (req.principalId == 0)
			return {};

		WorldActionPlan plan{ req };
		plan.status = eWorldActionStatus::Succeeded;
		const auto memberships = m_directory ? m_directory->FindUserMembershipEntry(req.principalId) : std::nullopt;

		switch (req.action)
		{
		case eWorldAction::Leave:
			plan.action	= eWorldAction::Leave;
			plan.execFlags = {};
			if (const WorldDesc* sourceDesc = ResolveDesc(req); sourceDesc && sourceDesc->destroyWhenEmpty)
				plan.execFlags.set(eWorldActionExecFlag::DestroySource);
				plan.source = req.source;
			if (memberships)
			{
				if (const auto* source = FindMembershipByKey(*memberships, req.source))
					plan.sourcePresence = source->presence;
			}
			plan.target = {};
			return plan;

		case eWorldAction::AutoAssign:
		case eWorldAction::Join:
		case eWorldAction::Transfer:
		case eWorldAction::Promote:
			break;

		default:
			plan.status = eWorldActionStatus::Failed;
			plan.reason = eWorldActionReason::InvalidArgument;
			return plan;
		}

		if (!m_directory)
		{
			plan.status = eWorldActionStatus::Failed;
			plan.reason = eWorldActionReason::TargetUnavailable;
			return plan;
		}

		const WorldDesc* desc = ResolveDesc(req);
		if (!desc)
		{
			plan.status = eWorldActionStatus::Failed;
			plan.reason = eWorldActionReason::InvalidArgument;
			return plan;
		}
		const uint32 templateId = ResolveDescId(req);
		plan.source = req.source;
		if (memberships)
		{
			if (const auto* source = req.source.IsValid() ? FindMembershipByKey(*memberships, req.source) : nullptr)
				plan.sourcePresence = source->presence;
		}
		if (!desc->allowMultipleInstancePerUser)
		{
			if (memberships)
			{
				if (const auto* existing = FindMembershipByDesc(*memberships, templateId))
				{
					if (req.action == eWorldAction::AutoAssign)
					{
						plan.action	= eWorldAction::Join;
						plan.target = existing->key;
						return plan;
					}

					if (req.target.IsIssued() && req.target == existing->key)
					{
						plan.status = eWorldActionStatus::Failed;
						plan.reason = eWorldActionReason::AlreadyInTarget;
						return plan;
					}

					if (req.action != eWorldAction::Transfer || req.source != existing->key)
					{
						plan.status = eWorldActionStatus::Failed;
						plan.reason = eWorldActionReason::ConflictingTransfer;
						return plan;
					}
				}
			}
		}

		if (memberships)
		{
			const auto* sourceMembership = req.source.IsValid() ? FindMembershipByKey(*memberships, req.source) : nullptr;
			const auto* targetMembership = req.target.IsValid() ? FindMembershipByKey(*memberships, req.target) : nullptr;
			const auto* currentActive = FindActivePhysicalMembership(*memberships);
			if (req.action == eWorldAction::Promote)
			{
				if (!targetMembership || targetMembership->key != req.target || desc->kind != eWorldKind::Physical)
				{
					plan.status = eWorldActionStatus::Failed;
					plan.reason = eWorldActionReason::InvalidArgument;
					return plan;
				}

				if (targetMembership->presence == eWorldMembershipPresence::Active)
				{
					plan.status = eWorldActionStatus::Failed;
					plan.reason = eWorldActionReason::AlreadyInTarget;
					return plan;
				}

				plan.action			   = eWorldAction::Promote;
				plan.execFlags		   = {};
				plan.target			   = targetMembership->key;
				plan.source			   = currentActive ? currentActive->key : req.source;
				plan.sourcePresence    = eWorldMembershipPresence::Active;
				plan.resultingPresence = eWorldMembershipPresence::Active;
				return plan;
			}

			if (desc->kind == eWorldKind::Physical)
			{
				if (req.action == eWorldAction::Transfer)
					plan.resultingPresence = sourceMembership ? sourceMembership->presence : eWorldMembershipPresence::Passive;
				else
					plan.resultingPresence = currentActive ? eWorldMembershipPresence::Passive : eWorldMembershipPresence::Active;
			}
			else
			{
				plan.resultingPresence = eWorldMembershipPresence::None;
			}
		}
		else
		{
			plan.resultingPresence = (desc->kind == eWorldKind::Physical)
				? eWorldMembershipPresence::Active
				: eWorldMembershipPresence::None;
		}

		plan.target = req.target;
		plan.action = (req.action == eWorldAction::Transfer && req.source.IsValid())
			? eWorldAction::Transfer
			: eWorldAction::Join;
		plan.execFlags = (plan.action == eWorldAction::Transfer)
			? WorldActionExecFlags{}
			: WorldActionExecFlags{};
		if (plan.action == eWorldAction::Transfer && desc->destroyWhenEmpty)
			plan.execFlags.set(eWorldActionExecFlag::DestroySource);

		if (req.action == eWorldAction::AutoAssign)
		{
			const std::vector<WorldMeta> worlds = m_directory->FindWorldsByDesc(templateId);
			if (const auto selected = SelectWorld(*desc, worlds))
			{
				plan.action	= eWorldAction::Join;
				plan.execFlags = {};
				plan.target = selected->key;
				return plan;
			}

			plan.action	= eWorldAction::Join;
			plan.execFlags = eWorldActionExecFlag::CreateTarget;
			plan.target = WorldKey{ .descId = templateId };
			return plan;
		}

		if (!plan.target.IsValid())
		{
			plan.status = eWorldActionStatus::Failed;
			plan.reason = eWorldActionReason::InvalidArgument;
			return plan;
		}

		if (plan.target.IsIssued())
		{
			const auto target = m_directory->FindWorld(plan.target);
			if (!target)
			{
				if (m_resolveMode == eWorldResolveMode::CreateIfMissing)
					plan.execFlags.set(eWorldActionExecFlag::CreateTarget);
				else
				{
					plan.status = eWorldActionStatus::Failed;
					plan.reason = eWorldActionReason::TargetUnavailable;
					return plan;
				}
			}
			else if (!target->HasCapacity())
			{
				plan.status = eWorldActionStatus::Failed;
				plan.reason = eWorldActionReason::CapacityExceeded;
				return plan;
			}
			else if (!IsJoinableRuntimeState(*target))
			{
				plan.status = eWorldActionStatus::Failed;
				plan.reason = eWorldActionReason::TargetUnavailable;
				return plan;
			}
		}

		if (m_resolveMode == eWorldResolveMode::CreateIfMissing)
		{
			plan.execFlags.set(eWorldActionExecFlag::CreateTarget);
		}

		return plan;
	}
}
