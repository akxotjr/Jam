#include "pch.h"
#include "jamnet/runtime/world/DefaultWorldAssignmentService.h"

#include "jamnet/runtime/ServerNetworkManager.h"
#include "jamnet/runtime/world/DefaultWorldAssignmentPolicy.h"
#include "jamnet/sync/networld/ServerNetWorld.h"

namespace jam::net
{
	DefaultWorldAssignmentService::DefaultWorldAssignmentService()
	{
		SetWorldAssignmentPolicy(std::make_unique<DefaultWorldAssignmentPolicy>());
	}

	void DefaultWorldAssignmentService::Init(ServerNetworkManager* owner)
	{
		m_netManager = owner;

		if (m_policy)
			m_policy->Init(owner);
	}

	void DefaultWorldAssignmentService::SetWorldAssignmentPolicy(std::unique_ptr<IWorldAssignmentPolicy> policy)
	{
		if (m_policy)
			m_policy->Init(nullptr);

		m_policy = std::move(policy);

		if (m_policy)
			m_policy->Init(m_netManager);
	}

	WorldAssignmentDecision DefaultWorldAssignmentService::BuildDecision(const WorldAssignmentRequest& req) const
	{
		if (!m_netManager || req.principalId == 0 || !m_policy)
			return {};

		const WorldAssignmentPolicyRequest policyReq
		{
			.principalId	= req.principalId,
			.currentWorldId = req.currentWorldId,
			.currentWorld	= req.currentWorld
		};

		const WorldAssignmentPolicyResult policyResult = m_policy->EvaluateAssignment(policyReq);

		WorldAssignmentDecision decision{};
		decision.status			= policyResult.status;
		decision.targetWorldId	= policyResult.worldId;
		decision.targetWorld	= policyResult.targetWorld;
		decision.options		= policyResult.worldOptions;

		if (decision.status != eWorldAssignmentStatus::Assigned)
		{
			decision.action = (decision.status == eWorldAssignmentStatus::Waiting)
				? eWorldAssignmentAction::None
				: eWorldAssignmentAction::Reject;
			return decision;
		}

		if (decision.targetWorldId == INVALID_WORLD_ID && decision.targetWorld.IsValid())
			decision.targetWorldId = m_netManager->ResolveWorldId(decision.targetWorld);

		if (decision.targetWorldId != INVALID_WORLD_ID && !decision.targetWorld.IsValid())
			decision.targetWorld = m_netManager->GetWorldKey(decision.targetWorldId);

		if (decision.targetWorldId == INVALID_WORLD_ID && !decision.targetWorld.IsValid())
		{
			decision.status = eWorldAssignmentStatus::Failed;
			decision.action = eWorldAssignmentAction::Reject;
			return decision;
		}

		if (policyResult.preferredAction != eWorldAssignmentAction::None)
		{
			decision.action = policyResult.preferredAction;
			return decision;
		}

		if (req.currentWorldId != INVALID_WORLD_ID &&
			decision.targetWorldId != INVALID_WORLD_ID &&
			req.currentWorldId != decision.targetWorldId)
		{
			decision.action = eWorldAssignmentAction::Transfer;
			return decision;
		}

		decision.action = (decision.targetWorldId != INVALID_WORLD_ID)
			? eWorldAssignmentAction::Join
			: eWorldAssignmentAction::Provision;

		return decision;
	}

	WorldAssignmentResult DefaultWorldAssignmentService::AssignPrincipal(const WorldAssignmentRequest& req)
	{
		const WorldAssignmentDecision decision = BuildDecision(req);

		WorldAssignmentResult result{};
		result.status		= decision.status;
		result.action		= decision.action;
		result.targetWorld	= decision.targetWorld;
		result.worldId		= decision.targetWorldId;

		if (!m_netManager || decision.status != eWorldAssignmentStatus::Assigned)
			return result;

		WorldId targetWorldId = decision.targetWorldId;
		if (targetWorldId == INVALID_WORLD_ID)
			targetWorldId = m_netManager->ResolveOrAllocateWorldId(decision.targetWorld, decision.options);

		if (targetWorldId == INVALID_WORLD_ID)
		{
			result.status = eWorldAssignmentStatus::Failed;
			result.action = eWorldAssignmentAction::Reject;
			return result;
		}

		if (decision.targetWorld == INVALID_WORLD_KEY)
			result.targetWorld = m_netManager->GetWorldKey(targetWorldId);

		result.worldId = targetWorldId;

		if (req.currentWorldId == targetWorldId)
			return result;

		if (decision.options.capacity != 0 &&
			m_netManager->GetWorldMemberCount(targetWorldId) >= decision.options.capacity)
		{
			result.status	= eWorldAssignmentStatus::Failed;
			result.action	= eWorldAssignmentAction::Reject;
			result.worldId	= INVALID_WORLD_ID;
			return result;
		}

		ServerNetWorld* targetWorld = m_netManager->GetOrCreateWorld(targetWorldId, decision.options);
		if (!targetWorld)
		{
			result.status	= eWorldAssignmentStatus::Failed;
			result.action	= eWorldAssignmentAction::Reject;
			result.worldId	= INVALID_WORLD_ID;
			return result;
		}

		if (req.currentWorldId != INVALID_WORLD_ID && req.currentWorldId != targetWorldId)
		{
			if (auto* prevWorld = m_netManager->GetWorld(req.currentWorldId))
				prevWorld->Leave(req.principalId);

			m_netManager->LeaveWorld(req.currentWorldId, req.principalId);
			m_netManager->TryDestroyWorldIfEmpty(req.currentWorldId);
		}

		m_netManager->JoinWorld(targetWorldId, req.principalId);
		targetWorld->Enter(req.principalId);

		return result;
	}
}
