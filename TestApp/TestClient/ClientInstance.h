#pragma once

#include "jamnet/runtime/ClientRuntime.h"
#include "jamnet/runtime/AppRuntimeEvents.h"

#include "jampx/PhysicsTypes.h"

using namespace std;
using namespace jam;

struct ClientInstanceConfig
{
	bool	headlessPhysicalWorld = false;
	bool	autoAssignOnReady = true;
	uint32	autoAssignTemplateId = 1;
};

enum class eClientType
{
	User,
	Bot
};


class ClientInstance
{
public:
	explicit ClientInstance(uint32 instanceId, uint64 accountId, ClientInstanceConfig config = {});
	virtual ~ClientInstance();

	bool                                Connect(const string& serverIp, uint16 tcpPort, uint16 udpPort);
	void                                Disconnect();
	bool                                IsConnected() const;

	virtual void                        Update(float deltaTime);
	virtual void                        Render();

	void								SpawnActor();
	virtual void						SpawnPlayer();
	void								SpawnPlayerDrone();
	void								SpawnBullet(const px::Vec3& muzzlePos, const px::Vec3& shootDir);

	void								DespawnActor();
	void								PossessActor();
	void								UnpossessActor();

	void								ControlCharacter(uint32 inputFlags, float pitch, float yaw, uint32 commandEpoch = 0);

	void                                SetWindowIndex(uint32 index) { m_windowIndex = index; }

	uint32                              GetInstanceId() const { return m_instanceId; }
	uint64                              GetAccountId() const { return m_accountId; }
	uint64                              GetUserId() const { return m_runtime ? m_runtime->GetUserId() : 0; }

	net::ClientRuntime*                 GetRuntime() const { return m_runtime.get(); }

protected:
	enum class SpawnKind : uint8
	{
		Player,
		PlayerDrone,
		Bullet
	};

	virtual void                        UpdateInput(float deltaTime) = 0;
	virtual void                        OnSpawnRequested(SpawnKind kind, uint32 spawnReqId) {}
	virtual void                        OnMainWorldChanged(net::LocalWorldId previousWorldId, net::LocalWorldId currentWorldId) {}
	virtual void                        OnActorLifecycle(const net::ActorLifecycleEvent& evt) {}
	virtual void                        OnClickMoveResolved(const net::ClickMoveResolvedEvent& evt) {}

	uint32                              GetWindowIndex() const { return m_windowIndex; }
	px::ObjectId                        GetLocalObjectId() const { return m_localObjectId; }
	net::LocalWorldId					GetMainWorldId() const { return m_mainWorld; }

protected:
	float                               m_yaw = 0.0f;
	float                               m_pitch = 0.0f;
	eClientType							m_type;

private:
	void                                RegisterRuntimeSubscriptions();
	void                                UnregisterRuntimeSubscriptions();
	void                                HandleNetworkState(const net::NetworkStateEvent& evt);
	void                                HandleWorldMembership(const net::WorldMembershipEvent& evt);
	void                                HandleActorLifecycle(const net::ActorLifecycleEvent& evt);
	void                                HandleClickMoveResolved(const net::ClickMoveResolvedEvent& evt);

private:
	uint32									m_instanceId	= 0;
	uint64									m_accountId		= 0;
	uint32									m_windowIndex	= 0;
	ClientInstanceConfig					m_config		= {};

	unique_ptr<net::ClientRuntime>			m_runtime = nullptr;

	uint32									m_nextSpawnReqId = 1;
	unordered_set<uint32>					m_pendingPlayerSpawnReqIds;

	GlobalEventBus::Subscription			m_subNetworkState;
	GlobalEventBus::Subscription			m_subWorldMembership;
	GlobalEventBus::Subscription			m_subActorLifecycle;
	GlobalEventBus::Subscription			m_subClickMoveResolved;

	px::ObjectId							m_localObjectId   = px::INVALID_OBJ_ID;

	net::LocalWorldId					m_mainWorld		  = net::kInvalidLocalWorldId;
	std::vector<net::LocalWorldId>		m_auxiliaryWorlds;

	bool									m_autoAssignRequested = false;
};
