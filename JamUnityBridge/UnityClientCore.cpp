#include "pch.h"
#include "UnityClientCore.h"

#include "jamnet/core/executor/MainExecutor.h"
#include "jampx/PhysicsCore.h"
#include "jampx/PhysicsFacade.h"

#include <algorithm>
#include <cmath>

namespace
{
	using namespace jam;

	JUNetworkPhase ToUnity(net::eNetworkPhase phase)
	{
		switch (phase)
		{
		case net::eNetworkPhase::Disconnected: return JUNetworkPhase::Disconnected;
		case net::eNetworkPhase::Connecting: return JUNetworkPhase::Connecting;
		case net::eNetworkPhase::Ready: return JUNetworkPhase::Ready;
		case net::eNetworkPhase::Degraded: return JUNetworkPhase::Degraded;
		default: return JUNetworkPhase::Disconnected;
		}
	}

	JUWorldMembershipChange ToUnity(net::eWorldMembershipChange change)
	{
		switch (change)
		{
		case net::eWorldMembershipChange::Joined: return JUWorldMembershipChange::Joined;
		case net::eWorldMembershipChange::Left: return JUWorldMembershipChange::Left;
		case net::eWorldMembershipChange::Promoted: return JUWorldMembershipChange::Promoted;
		case net::eWorldMembershipChange::Transferred: return JUWorldMembershipChange::Transferred;
		case net::eWorldMembershipChange::Updated: return JUWorldMembershipChange::Updated;
		default: return JUWorldMembershipChange::Updated;
		}
	}

	JUVec3 ToUnity(const px::Vec3& v)
	{
		return JUVec3{ v.x, v.y, v.z };
	}

	JUQuat ToUnity(const px::Quat& q)
	{
		return JUQuat{ q.x, q.y, q.z, q.w };
	}

	px::Vec3 ToPx(const JUVec3& v)
	{
		return px::Vec3{ v.x, v.y, v.z };
	}

	uint32_t ToPxInputFlags(uint32_t flags)
	{
		uint32_t out = px::INPUT_NONE;
		if (flags & JU_INPUT_FORWARD) out |= px::INPUT_FORWARD;
		if (flags & JU_INPUT_BACKWARD) out |= px::INPUT_BACKWARD;
		if (flags & JU_INPUT_LEFT) out |= px::INPUT_LEFT;
		if (flags & JU_INPUT_RIGHT) out |= px::INPUT_RIGHT;
		if (flags & JU_INPUT_CROUCH) out |= px::INPUT_CROUCH;
		if (flags & JU_INPUT_PRONE) out |= px::INPUT_PRONE;
		if (flags & JU_INPUT_RUN) out |= px::INPUT_RUN;
		if (flags & JU_INPUT_SPRINT) out |= px::INPUT_SPRINT;
		if (flags & JU_INPUT_JUMP) out |= px::INPUT_JUMP;
		if (flags & JU_INPUT_DASH) out |= px::INPUT_DASH;
		return out;
	}
}

bool UnityClientCore::Initialize(const JUClientConfig& config)
{
	if (m_runtime)
		return true;

	m_accountId = config.accountId;
	m_instanceId = config.instanceId;
	m_autoAssignArchetypeKey = jam::net::WorldArchetypeKey::FromU64(config.autoAssignArchetypeKey);
	m_autoAssignOnReady = config.autoAssignOnReady != 0;

	jam::net::RuntimeConfig runtimeConfig{};
	runtimeConfig.geConfig.autoTune = true;
	runtimeConfig.geConfig.layoutCfg = { .mode = jam::Balance, .reservedThreads = 1, .profile = jam::CoreProfileClient };
	m_netRuntime = std::make_unique<jam::net::NetRuntime>(runtimeConfig);

	if (!config.headlessWorld)
	{
		PHYSICS_CORE_INIT();
		m_physicsInitialized = true;
	}

	jam::net::ClientConfig clientConfig{};
	clientConfig.accountId				= config.accountId;
	clientConfig.serverTcpAddress		= jam::net::NetAddress(config.serverIp ? config.serverIp : "127.0.0.1", config.tcpPort);
	clientConfig.serverUdpAddress		= jam::net::NetAddress(config.serverIp ? config.serverIp : "127.0.0.1", config.udpPort);
	clientConfig.headlessWorld			= config.headlessWorld != 0;
	clientConfig.sharedDataCatalogPath	= config.sharedDataCatalogAssetPath ? config.sharedDataCatalogAssetPath : "";
	clientConfig.worldTemplatePath		= config.worldTemplateAssetPath ? config.worldTemplateAssetPath : "";
	clientConfig.worldArchetypePath		= config.worldArchetypeAssetPath ? config.worldArchetypeAssetPath : "";
	if (!clientConfig.headlessWorld)
		clientConfig.physicsFactory = [] { return std::make_unique<jam::px::PhysicsFacade>(); };

	m_runtime = std::make_unique<jam::net::ClientRuntime>(clientConfig);
	RegisterRuntimeSubscriptions();

	if (!m_runtime->Connect())
	{
		Shutdown();
		return false;
	}

	return true;
}

void UnityClientCore::Shutdown()
{
	if (m_runtime)
	{
		UnregisterRuntimeSubscriptions();
		m_runtime->Disconnect();
		m_runtime.reset();
	}

	m_pendingPlayerSpawnReqIds.clear();
	m_localObjectId = jam::px::INVALID_OBJ_ID;
	m_mainWorld = jam::net::kInvalidLocalWorldId;
	m_mainWorldArchetypeKey = {};
	m_autoAssignRequested = false;
	m_eventQueue.Clear();

	if (m_physicsInitialized)
	{
		PHYSICS_CORE_SHUTDOWN();
		m_physicsInitialized = false;
	}

	m_netRuntime.reset();
}

void UnityClientCore::Pump(float deltaTime)
{
	(void)deltaTime;
	MAIN_EXEC.PumpOnce();

	RequestAutoAssignIfReady();
	SpawnPlayerIfNeeded();
}

void UnityClientCore::SubmitInput(const JUInputCommand& command)
{
	if (!m_runtime)
		return;

	m_runtime->PushInput(
		ToPxInputFlags(command.inputFlags),
		command.facingPitch,
		command.facingYaw,
		command.commandEpoch);
}

void UnityClientCore::RequestClickMove(const JUClickMoveCommand& command)
{
	if (!m_runtime)
		return;

	m_runtime->RequestClickMove(
		ToPx(command.rayOrigin),
		ToPx(command.rayDirection),
		command.maxRange,
		command.requestSeq,
		command.commandEpoch,
		command.facingYaw);
}

bool UnityClientCore::IsConnected() const
{
	return m_runtime && m_runtime->GetNetworkState().phase != jam::net::eNetworkPhase::Disconnected;
}

JUNetworkPhase UnityClientCore::GetNetworkPhase() const
{
	return m_runtime ? ToUnity(m_runtime->GetNetworkState().phase) : JUNetworkPhase::Disconnected;
}

uint64_t UnityClientCore::GetUserId() const
{
	return m_runtime ? m_runtime->GetUserId() : 0;
}

int32_t UnityClientCore::CopyActorFrame(JUActorState* outActors, int32_t actorCapacity, JUActorFrame* outFrame) const
{
	if (!m_runtime || !outFrame)
		return 0;

	const jam::net::LocalWorldId localWorldId = m_runtime->GetMainLocalWorldId();
	const jam::net::ActorPresentationFrameView frame = m_runtime->GetActorPresentationFrame(localWorldId);
	outFrame->sequence = frame.sequence;
	outFrame->tick = frame.tick;
	outFrame->timestamp = frame.timestamp;
	outFrame->actorCount = static_cast<int32_t>(frame.actors.size());

	if (!outActors || actorCapacity <= 0)
		return outFrame->actorCount;

	const int32_t copyCount = std::min<int32_t>(actorCapacity, outFrame->actorCount);
	for (int32_t i = 0; i < copyCount; ++i)
	{
		const jam::net::ActorPresentationState& actor = frame.actors[static_cast<size_t>(i)];
		JUActorState& out = outActors[i];
		out = {};
		out.objectId = actor.objectId;
		out.isLevelActor = actor.netId.IsLevel() ? 1 : 0;
		out.isLocal = actor.isLocal ? 1 : 0;

		if (actor.cs.has_value())
		{
			out.hasTransform = 1;
			out.position = ToUnity(actor.cs->pos);
			out.rotation = ToUnity(jam::px::Quat::FromYawPitch(actor.cs->facingYaw, actor.cs->facingPitch));
		}
		else if (actor.rs.has_value())
		{
			out.hasTransform = 1;
			out.position = ToUnity(actor.rs->pose.p);
			out.rotation = ToUnity(actor.rs->pose.q);
		}
	}

	return copyCount;
}

void UnityClientCore::RegisterRuntimeSubscriptions()
{
	if (!m_runtime)
		return;

	const jam::SubscribeOptions opt{ jam::eDispatchPolicy::MainExecutor };
	m_subNetworkState = m_runtime->SubscribeNetworkState(
		[this](const jam::net::NetworkStateEvent& evt) { HandleNetworkState(evt); }, opt);
	m_subWorldMembership = m_runtime->SubscribeWorldMembership(
		[this](const jam::net::WorldMembershipEvent& evt) { HandleWorldMembership(evt); }, opt);
	m_subActorLifecycle = m_runtime->SubscribeActorLifecycle(
		[this](const jam::net::ActorLifecycleEvent& evt) { HandleActorLifecycle(evt); }, opt);
	m_subClickMoveResolved = m_runtime->SubscribeClickMoveResolved(
		[this](const jam::net::ClickMoveResolvedEvent& evt) { HandleClickMoveResolved(evt); }, opt);
}

void UnityClientCore::UnregisterRuntimeSubscriptions()
{
	GLOBAL_EVENTBUS_UNSUBSCRIBE(m_subNetworkState.type, m_subNetworkState.id);
	GLOBAL_EVENTBUS_UNSUBSCRIBE(m_subWorldMembership.type, m_subWorldMembership.id);
	GLOBAL_EVENTBUS_UNSUBSCRIBE(m_subActorLifecycle.type, m_subActorLifecycle.id);
	GLOBAL_EVENTBUS_UNSUBSCRIBE(m_subClickMoveResolved.type, m_subClickMoveResolved.id);
	m_subNetworkState = {};
	m_subWorldMembership = {};
	m_subActorLifecycle = {};
	m_subClickMoveResolved = {};
}

void UnityClientCore::HandleNetworkState(const jam::net::NetworkStateEvent& evt)
{
	m_eventQueue.Push(JUNetworkStateEvent
	{
		.phase = ToUnity(evt.state.phase),
		.accountId = evt.accountId,
		.userId = evt.userId
	});

	if (evt.state.phase != jam::net::eNetworkPhase::Ready)
	{
		m_mainWorld = jam::net::kInvalidLocalWorldId;
		m_mainWorldArchetypeKey = {};
		m_autoAssignRequested = false;
	}
}

void UnityClientCore::HandleWorldMembership(const jam::net::WorldMembershipEvent& evt)
{
	m_eventQueue.Push(JUWorldMembershipEvent
	{
		.change = ToUnity(evt.change),
		.localWorldId = evt.membership.localWorldId,
		.worldId = evt.membership.key.worldId,
		.archetypeKey = evt.membership.key.archetypeKey.v
	});

	const auto mainMembership = m_runtime ? m_runtime->GetMainWorldMembership() : std::nullopt;
	const jam::net::LocalWorldId nextMainWorld = mainMembership.has_value()
		? mainMembership->localWorldId
		: jam::net::kInvalidLocalWorldId;

	if (nextMainWorld != m_mainWorld)
	{
		m_pendingPlayerSpawnReqIds.clear();
		m_localObjectId = jam::px::INVALID_OBJ_ID;
		m_mainWorld = nextMainWorld;
	}

	m_mainWorldArchetypeKey = mainMembership.has_value()
		? mainMembership->key.archetypeKey
		: jam::net::WorldArchetypeKey{};

	if (nextMainWorld != jam::net::kInvalidLocalWorldId)
		m_autoAssignRequested = false;
}

void UnityClientCore::HandleActorLifecycle(const jam::net::ActorLifecycleEvent& evt)
{
	if (evt.reason == jam::net::eActorLifecycleReason::Spawned
		&& m_pendingPlayerSpawnReqIds.erase(evt.spawnReqId) > 0
		&& evt.isLocal)
	{
		m_localObjectId = evt.objectId;
	}

	if ((evt.reason == jam::net::eActorLifecycleReason::Despawned
		|| evt.reason == jam::net::eActorLifecycleReason::PredictedDespawn)
		&& evt.objectId == m_localObjectId)
	{
		m_localObjectId = jam::px::INVALID_OBJ_ID;
	}

	if (evt.reason == jam::net::eActorLifecycleReason::Spawned
		|| evt.reason == jam::net::eActorLifecycleReason::AoiEntered)
	{
		m_eventQueue.Push(JUActorSpawnedEvent
		{
			.objectId = evt.objectId,
			.actorArchetypeKey = evt.actorArchetypeKey.v,
			.isLevelActor = evt.netId.IsLevel() ? 1 : 0,
			.isLocal = evt.isLocal ? 1 : 0
		});
		return;
	}

	if (evt.reason == jam::net::eActorLifecycleReason::Despawned
		|| evt.reason == jam::net::eActorLifecycleReason::AoiLeft
		|| evt.reason == jam::net::eActorLifecycleReason::PredictedDespawn)
	{
		m_eventQueue.Push(JUActorDespawnedEvent
		{
			.objectId = evt.objectId
		});
	}
}

void UnityClientCore::HandleClickMoveResolved(const jam::net::ClickMoveResolvedEvent& evt)
{
	m_eventQueue.Push(JUClickMoveResolvedEvent
	{
		.requestSeq = evt.requestSeq,
		.followTarget = evt.followTarget ? 1 : 0,
		.targetPos = ToUnity(evt.targetPos)
	});
}

void UnityClientCore::RequestAutoAssignIfReady()
{
	if (!m_runtime || !m_autoAssignOnReady || m_autoAssignRequested)
		return;

	if (m_runtime->GetNetworkState().phase != jam::net::eNetworkPhase::Ready)
		return;

	if (!m_runtime->GetWorldMemberships().empty())
		return;

	const jam::net::WorldKey targetKey{ .archetypeKey = m_autoAssignArchetypeKey };
	if (!targetKey.IsValid())
		return;

	m_autoAssignRequested = true;
	m_runtime->RequestWorldAction(jam::net::eWorldAction::AutoAssign, {}, targetKey);
}

void UnityClientCore::SpawnPlayerIfNeeded()
{
	if (!m_runtime)
		return;

	if (m_mainWorld == jam::net::kInvalidLocalWorldId)
		return;

	if (m_localObjectId != jam::px::INVALID_OBJ_ID || !m_pendingPlayerSpawnReqIds.empty())
		return;

	jam::px::Vec3 pos{ 5.0f * static_cast<float>(m_instanceId), 10.0f, 0.0f };

	jam::net::SpawnParams params{};
	params.spawnId = m_nextSpawnReqId++;
	params.owned = true;
	params.controlled = true;
	params.desc.archetype = jam::px::MakePhysicsArchetypeKey("Character");
	params.actorArchetypeKey = jam::net::MakeActorArchetypeKey("Character");
	params.desc.pose = { .p = pos };
	params.desc.overrides = jam::px::CharacterSpawnOverrides{};

	m_pendingPlayerSpawnReqIds.insert(params.spawnId);
	m_runtime->RequestSpawnActor(params);
}
