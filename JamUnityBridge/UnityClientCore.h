#pragma once

#include <memory>
#include <set>

#include "JamUnityBridge.h"
#include "UnityEventQueue.h"
#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/net/NetRuntime.h"
#include "jamnet/runtime/ClientRuntime.h"

class UnityClientCore
{
public:
	bool Initialize(const JUClientConfig& config);
	void Shutdown();

	void Pump(float deltaTime);
	void SubmitInput(const JUInputCommand& command);
	void RequestClickMove(const JUClickMoveCommand& command);

	bool IsConnected() const;
	JUNetworkPhase GetNetworkPhase() const;
	uint64_t GetUserId() const;
	jam::net::LocalWorldId GetMainLocalWorldId() const { return m_mainWorld; }
	uint64_t GetMainWorldArchetypeKey() const { return m_mainWorldArchetypeKey.v; }
	int32_t CopyActorFrame(JUActorState* outActors, int32_t actorCapacity, JUActorFrame* outFrame) const;

	UnityEventQueue& GetEventQueue() { return m_eventQueue; }

private:
	void RegisterRuntimeSubscriptions();
	void UnregisterRuntimeSubscriptions();

	void HandleNetworkState(const jam::net::NetworkStateEvent& evt);
	void HandleWorldMembership(const jam::net::WorldMembershipEvent& evt);
	void HandleActorLifecycle(const jam::net::ActorLifecycleEvent& evt);
	void HandleClickMoveResolved(const jam::net::ClickMoveResolvedEvent& evt);

	void RequestAutoAssignIfReady();
	void SpawnPlayerIfNeeded();

private:
	std::unique_ptr<jam::net::NetRuntime>		m_netRuntime;
	std::unique_ptr<jam::net::ClientRuntime>	m_runtime;

	jam::GlobalEventBus::Subscription			m_subNetworkState;
	jam::GlobalEventBus::Subscription			m_subWorldMembership;
	jam::GlobalEventBus::Subscription			m_subActorLifecycle;
	jam::GlobalEventBus::Subscription			m_subClickMoveResolved;

	UnityEventQueue								m_eventQueue;

	uint64_t									m_accountId = 0;
	uint32_t									m_instanceId = 0;
	jam::net::WorldArchetypeKey					m_autoAssignArchetypeKey = {};
	bool										m_autoAssignOnReady = true;
	bool										m_autoAssignRequested = false;
	bool										m_physicsInitialized = false;

	jam::net::LocalWorldId						m_mainWorld = jam::net::kInvalidLocalWorldId;
	jam::net::WorldArchetypeKey					m_mainWorldArchetypeKey = {};
	jam::px::ObjectId							m_localObjectId = jam::px::INVALID_OBJ_ID;
	uint32_t									m_nextSpawnReqId = 1;
	std::set<uint32_t>							m_pendingPlayerSpawnReqIds;
};
