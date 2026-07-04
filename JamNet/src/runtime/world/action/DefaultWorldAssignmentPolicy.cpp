#include "pch.h"
#include "jamnet/runtime/world/action/DefaultWorldAssignmentPolicy.h"

#include "jamnet/runtime/world/core/WorldDirectory.h"

#include <algorithm>

namespace jam::net
{
	namespace
	{
		const WorldMembership* FindMembershipByArchetype(const UserMembershipSnapshotEntry& memberships, WorldArchetypeKey archetypeKey)
		{
			auto it = std::ranges::find_if(memberships.memberships, [archetypeKey](const WorldMembership& membership)
				{
					return membership.key.archetypeKey == archetypeKey;
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

	WorldArchetypeKey DefaultWorldAssignmentPolicy::ResolveArchetypeKey(const WorldActionRequest& req) const
	{
		if (IsValidAssetKey(req.target.archetypeKey))
			return req.target.archetypeKey;
		if (IsValidAssetKey(req.source.archetypeKey))
			return req.source.archetypeKey;
		return {};
	}

	const WorldTemplateData* DefaultWorldAssignmentPolicy::ResolveTemplate(const WorldActionRequest& req) const
	{
		const WorldArchetypeKey archetypeKey = ResolveArchetypeKey(req);
		if (!IsValidAssetKey(archetypeKey) || !m_archetypes || !m_templates)
			return nullptr;

		const auto* archetype = m_archetypes->Find(archetypeKey);
		if (!archetype)
			return nullptr;

		return m_templates->Find(archetype->templateKey);
	}

	std::optional<WorldMeta> DefaultWorldAssignmentPolicy::SelectWorld(
		const WorldTemplateData& templateData,
		std::span<const WorldMeta> candidates) const
	{
		(void)templateData;
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
			if (const WorldTemplateData* sourceTemplate = ResolveTemplate(req); sourceTemplate && sourceTemplate->destroyWhenEmpty)
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

		const WorldTemplateData* templateData = ResolveTemplate(req);
		if (!templateData)
		{
			plan.status = eWorldActionStatus::Failed;
			plan.reason = eWorldActionReason::InvalidArgument;
			return plan;
		}
		const WorldArchetypeKey archetypeKey = ResolveArchetypeKey(req);
		plan.source = req.source;
		if (memberships)
		{
			if (const auto* source = req.source.IsValid() ? FindMembershipByKey(*memberships, req.source) : nullptr)
				plan.sourcePresence = source->presence;
		}
		if (!templateData->allowMultipleInstancePerUser)
		{
			if (memberships)
			{
				if (const auto* existing = FindMembershipByArchetype(*memberships, archetypeKey))
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
				if (!targetMembership || targetMembership->key != req.target || templateData->kind != eWorldKind::Physical)
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

			if (templateData->kind == eWorldKind::Physical)
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
			plan.resultingPresence = (templateData->kind == eWorldKind::Physical)
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
		if (plan.action == eWorldAction::Transfer && templateData->destroyWhenEmpty)
			plan.execFlags.set(eWorldActionExecFlag::DestroySource);

		if (req.action == eWorldAction::AutoAssign)
		{
			const std::vector<WorldMeta> worlds = m_directory->FindWorldsByArchetype(archetypeKey);
			if (const auto selected = SelectWorld(*templateData, worlds))
			{
				plan.action	= eWorldAction::Join;
				plan.execFlags = {};
				plan.target = selected->key;
				return plan;
			}

			plan.action	= eWorldAction::Join;
			plan.execFlags = eWorldActionExecFlag::CreateTarget;
			plan.target = WorldKey{ .archetypeKey = archetypeKey };
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
