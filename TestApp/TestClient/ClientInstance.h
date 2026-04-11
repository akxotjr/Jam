#pragma once

#include "jamnet/runtime/ClientNetworkManager.h"
#include "jamnet/sync/replication/ReplicationEvents.h"

#include "jampx/PhysicsTypes.h"

using namespace jam;

class ClientInstance
{
public:
	explicit ClientInstance(uint32 instanceId, uint64 userId);
	virtual ~ClientInstance();

	bool                                Connect(const string& serverIp, uint16 tcpPort, uint16 udpPort);
	void                                Disconnect();
	bool                                IsConnected() const;

	virtual void                        Update(float deltaTime);
	virtual void                        Render();

	void								SpawnActor();
	void								SpawnPlayer();
	void								SpawnPlayerDrone();
	void								SpawnBullet(const px::Vec3& muzzlePos, const px::Vec3& shootDir);

	void								DespawnActor();
	void								PossessActor();
	void								UnpossessActor();

	void								ControlCharacter(uint32 inputFlags, float pitch, float yaw, uint32 commandEpoch = 0);

	void                                SetWindowIndex(uint32 index) { m_windowIndex = index; }

	uint32                              GetInstanceId() const { return m_instanceId; }
	uint64                              GetUserId() const { return m_userId; }

	net::ClientNetworkManager*          GetNetworkManager() const { return m_networkManager.get(); }

protected:
	enum class SpawnKind : uint8
	{
		Player,
		PlayerDrone,
		Bullet
	};

	virtual void                        UpdateInput(float deltaTime) = 0;
	virtual void                        OnSpawnRequested(SpawnKind kind, uint32 spawnReqId) {}
	virtual void                        OnLevelSpawned(const net::RenderLevelSpawnedEvent& evt) {}
	virtual void                        OnActorSpawned(const net::RenderActorSpawnedEvent& evt) {}
	virtual void                        OnActorDespawned(const net::RenderActorDespawnedEvent& evt) {}
	virtual void                        OnClickMoveResolved(const net::ClickMoveResolvedEvent& evt) {}
	virtual void                        OnRenderSamples(const net::RenderSamplesEvent& evt) {}

	uint32                              GetWindowIndex() const { return m_windowIndex; }
	px::ObjectId                        GetLocalObjectId() const { return m_localObjectId; }

protected:
	float                               m_yaw = 0.0f;
	float                               m_pitch = 0.0f;

private:
	void                                HandleSessionReady(const net::ClientSessionReadyEvent& evt);
	void                                HandleWorldRequestResult(const net::WorldRequestResultEvent& evt);
	void                                HandleWorldAssignmentSucceeded(const net::WorldAssignmentSucceededEvent& evt);
	void                                HandleLevelSpawned(const net::RenderLevelSpawnedEvent& evt);
	void                                HandleActorSpawned(const net::RenderActorSpawnedEvent& evt);
	void                                HandleActorDespawned(const net::RenderActorDespawnedEvent& evt);
	void                                HandleClickMoveResolved(const net::ClickMoveResolvedEvent& evt);
	void                                HandleRenderSamples(const net::RenderSamplesEvent& evt);

private:
	uint32									m_instanceId	= 0;
	uint64									m_userId		= 0;
	uint32									m_windowIndex	= 0;

	unique_ptr<net::ClientNetworkManager>	m_networkManager = nullptr;

	uint32									m_nextSpawnReqId = 1;
	unordered_set<uint32>					m_pendingPlayerSpawnReqIds;

	GlobalEventBus::Subscription			m_subLevelSpawned;
	GlobalEventBus::Subscription			m_subActorSpawned;
	GlobalEventBus::Subscription			m_subActorDespawned;
	GlobalEventBus::Subscription			m_subClickMoveResolved;
	GlobalEventBus::Subscription			m_subRenderSamples;
	GlobalEventBus::Subscription			m_subSessionReady;
	GlobalEventBus::Subscription			m_subWorldRequestResult;
	GlobalEventBus::Subscription			m_subWorldAssignmentSucceeded;

	px::ObjectId							m_localObjectId = px::INVALID_OBJ_ID;
	net::WorldId							m_assignedWorldId = net::INVALID_WORLD_ID;
};
