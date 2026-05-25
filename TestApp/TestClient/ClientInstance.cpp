#include "pch.h"
#include "ClientInstance.h"

#include "jamnet/runtime/ClientSession.h"
#include "jamnet/sync/networld/ClientPhysicalWorld.h"
#include "jampx/PhysicsFacade.h"

ClientInstance::ClientInstance(uint32 instanceId, uint64 accountId, ClientInstanceConfig config)
	: m_instanceId(instanceId), m_accountId(accountId), m_config(config)
{
	JAMNET_LOG_INFO("[Client #{}] Created", m_instanceId);
}

ClientInstance::~ClientInstance()
{
	Disconnect();
	JAMNET_LOG_INFO("[Client #{}] Destroyed", m_instanceId);
}

bool ClientInstance::Connect(const string& serverIp, uint16 tcpPort, uint16 udpPort)
{
	if (m_runtime)
	{
		JAMNET_LOG_WARN("[Client #{}] Already connected", m_instanceId);
		return true;
	}

	net::ClientConfig config{};
	config.accountId		= m_accountId;
	config.serverTcpAddress = net::NetAddress(serverIp, tcpPort);
	config.serverUdpAddress = net::NetAddress(serverIp, udpPort);
	config.headlessWorld	= m_config.headlessPhysicalWorld;
	if (!config.headlessWorld)
		config.physicsFactory = [] { return std::make_unique<jam::px::PhysicsFacade>(); };
	config.worldAssetPath	= "C://Users//akxotjr//GameWorkSpace//Jam-dev//TestApp//Contents//world_templates.json";

	m_runtime = std::make_unique<net::ClientRuntime>(config);
	RegisterRuntimeSubscriptions();

	if (!m_runtime->Connect())
	{
		JAMNET_LOG_ERROR("[Client #{}] Failed to try connecting to server", m_instanceId);
		UnregisterRuntimeSubscriptions();
		m_runtime.reset();
		return false;
	}

	//JAMNET_LOG_INFO("[Client #{}] success to try connecting to server (accountId= {})", m_instanceId, m_accountId);
	return true;
}

void ClientInstance::Disconnect()
{
	if (!m_runtime)
		return;

	UnregisterRuntimeSubscriptions();
	m_runtime->Disconnect();
	m_runtime.reset();

	m_pendingPlayerSpawnReqIds.clear();
	m_localObjectId = px::INVALID_OBJ_ID;
	m_mainWorld = net::kInvalidLocalWorldId;
	m_auxiliaryWorlds.clear();
	m_autoAssignRequested = false;

	JAMNET_LOG_INFO("[Client #{}] Disconnected", m_instanceId);
}

bool ClientInstance::IsConnected() const
{
	return m_runtime && m_runtime->GetNetworkState().phase != net::eNetworkPhase::Disconnected;
}

void ClientInstance::Update(float deltaTime)
{
	if (!m_runtime)
		return;

	UpdateInput(deltaTime);
}

void ClientInstance::Render()
{
}

void ClientInstance::SpawnActor()
{
}

void ClientInstance::SpawnPlayer()
{
	if (!m_runtime) return;

	px::Vec3 pos;
	if (m_type == eClientType::User)
	{
		pos = { 5.0f * static_cast<float>(m_instanceId), 10.f, 0.f };
	}
	else
	{
		pos = { static_cast<float>(m_instanceId % 10) * 10.f, 5.f, -10.f - (static_cast<float>(m_instanceId) / 10.f) * 10.f };
	}

	net::SpawnParams charParams{};
	charParams.spawnId		  = m_nextSpawnReqId++;
	charParams.owned		  = true;
	charParams.controlled	  = true;

	charParams.desc.prefab    = px::MakePrefabKey("Character");
	charParams.desc.pose	  = { .p = pos };
	charParams.desc.overrides = px::CharacterSpawnOverrides{};

	m_pendingPlayerSpawnReqIds.insert(charParams.spawnId);
	OnSpawnRequested(SpawnKind::Player, charParams.spawnId);

	m_runtime->RequestSpawnActor(charParams);
}

void ClientInstance::SpawnPlayerDrone()
{
	//if (!m_networkManager) return;

	//auto* world = m_networkManager->GetWorld();
	//if (!world) return;

	//net::SpawnParams droneParams{};
	//droneParams.spawnId			= m_nextSpawnReqId++;
	//droneParams.owned			= true;
	//droneParams.controlled		= false;
	//droneParams.targetObjectId	= m_localObjectId;

	//droneParams.desc.prefab		= px::MakePrefabKey("OrbitDrone");
	//droneParams.desc.spawnSrc	= px::eSpawnSource::Runtime;
	//droneParams.desc.pose		= {};
	//droneParams.desc.overrides	= px::RigidSpawnOverrides{};

	//OnSpawnRequested(SpawnKind::PlayerDrone, droneParams.spawnId);
	//world->SpawnActor(droneParams);
}

void ClientInstance::SpawnBullet(const px::Vec3& muzzlePos, const px::Vec3& shootDir)
{
	/*if (!m_networkManager) return;

	auto* world = m_networkManager->GetWorld();
	if (!world) return;

	const px::Vec3 dir		= shootDir.GetNormalized();
	const px::Vec3 finalDir = dir.IsZero() ? px::Vec3(0.0f, 0.0f, 1.0f) : dir;

	static constexpr float k_bulletSpeed = 10.f;

	const float yaw   = std::atan2(finalDir.x, finalDir.z);
	const float horiz = std::sqrt(finalDir.x * finalDir.x + finalDir.z * finalDir.z);
	const float pitch = std::atan2(finalDir.y, (horiz > 1e-6f) ? horiz : 1e-6f);


	net::SpawnParams bulletParams{};
	bulletParams.spawnId		= m_nextSpawnReqId++;
	bulletParams.owned			= true;
	bulletParams.controlled		= false;
	bulletParams.targetObjectId = m_localObjectId;

	bulletParams.desc.prefab	= px::MakePrefabKey("LinearProjectile");
	bulletParams.desc.spawnSrc	= px::eSpawnSource::Runtime;
	bulletParams.desc.pose		= {
		.p = muzzlePos,
		.q = px::Quat::FromYawPitch(yaw, pitch)
	};
	bulletParams.desc.overrides = px::RigidSpawnOverrides{
		.mask			= px::SpawnOverrideMask::LINEAR_VEL,
		.linearVelocity = finalDir * k_bulletSpeed
	};

	OnSpawnRequested(SpawnKind::Bullet, bulletParams.spawnId);
	world->SpawnActor(bulletParams);*/
}

void ClientInstance::DespawnActor()
{
}

void ClientInstance::PossessActor()
{
}

void ClientInstance::UnpossessActor()
{
}

void ClientInstance::ControlCharacter(uint32 inputFlags, float pitch, float yaw, uint32 commandEpoch)
{
	if (!m_runtime) return;

	m_runtime->PushInput(inputFlags, pitch, yaw, commandEpoch);
}

void ClientInstance::RegisterRuntimeSubscriptions()
{
	if (!m_runtime)
		return;

	const jam::SubscribeOptions opt{ jam::eDispatchPolicy::MainExecutor };
	m_subNetworkState = m_runtime->SubscribeNetworkState(
		[this](const net::NetworkStateEvent& evt) { HandleNetworkState(evt); }, opt);
	m_subWorldMembership = m_runtime->SubscribeWorldMembership(
		[this](const net::WorldMembershipEvent& evt) { HandleWorldMembership(evt); }, opt);
	m_subActorLifecycle = m_runtime->SubscribeActorLifecycle(
		[this](const net::ActorLifecycleEvent& evt) { HandleActorLifecycle(evt); }, opt);
	m_subClickMoveResolved = m_runtime->SubscribeClickMoveResolved(
		[this](const net::ClickMoveResolvedEvent& evt) { HandleClickMoveResolved(evt); }, opt);
}

void ClientInstance::UnregisterRuntimeSubscriptions()
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

void ClientInstance::HandleNetworkState(const net::NetworkStateEvent& evt)
{
	if (evt.state.phase != net::eNetworkPhase::Ready)
	{
		m_autoAssignRequested = false;
		return;
	}

	if (!m_config.autoAssignOnReady || m_autoAssignRequested || !m_runtime)
		return;

	if (!m_runtime->GetWorldMemberships().empty())
		return;

	const net::WorldKey targetKey{ .descId = m_config.autoAssignTemplateId };
	if (!targetKey.IsValid())
		return;

	m_autoAssignRequested = true;
	m_runtime->RequestWorldAction(net::eWorldAction::AutoAssign, {}, targetKey);
	//JAMNET_LOG_INFO("[Client #{}] Requested auto world assignment: templateId={}", m_instanceId, targetKey.descId);
}

void ClientInstance::HandleWorldMembership(const net::WorldMembershipEvent& evt)
{
	if (!m_runtime)
		return;

	m_auxiliaryWorlds.clear();
	for (const net::WorldMembershipView& membership : m_runtime->GetWorldMemberships())
	{
		if (membership.role == net::eWorldRole::Auxiliary && membership.localWorldId != net::kInvalidLocalWorldId)
			m_auxiliaryWorlds.push_back(membership.localWorldId);
	}

	const auto mainMembership = m_runtime->GetMainWorldMembership();
	const net::LocalWorldId nextMainWorld = mainMembership.has_value()
		? mainMembership->localWorldId
		: net::kInvalidLocalWorldId;
	const bool mainWorldChanged = nextMainWorld != m_mainWorld;

	if (mainWorldChanged)
	{
		const net::LocalWorldId previousWorld = m_mainWorld;
		m_pendingPlayerSpawnReqIds.clear();
		m_localObjectId = px::INVALID_OBJ_ID;
		m_mainWorld = nextMainWorld;
		OnMainWorldChanged(previousWorld, nextMainWorld);
	}

	if (evt.change == net::eWorldMembershipChange::Left && nextMainWorld == net::kInvalidLocalWorldId)
		return;

	if (nextMainWorld == net::kInvalidLocalWorldId)
		return;

	const bool eventTouchesMainWorld = evt.membership.localWorldId == nextMainWorld
		|| (mainMembership.has_value() && evt.membership.key == mainMembership->key);
	if (!mainWorldChanged && !eventTouchesMainWorld)
		return;

	m_autoAssignRequested = false;
	if (m_localObjectId != px::INVALID_OBJ_ID || !m_pendingPlayerSpawnReqIds.empty())
	{
		//JAMNET_LOG_DEBUG("[Client #{}] Skip SpawnPlayer on duplicate main-world membership update: localWorldId={}", m_instanceId, nextMainWorld);
		return;
	}

	//JAMNET_LOG_INFO("[Client #{}] Main world ready: localWorldId={}", m_instanceId, nextMainWorld);
	JAMNET_LOG_DEBUG("[ClientInstance::HandleWorldMembership] account id= {} call SpawnPlayer", m_accountId);
	SpawnPlayer();
}

void ClientInstance::HandleActorLifecycle(const net::ActorLifecycleEvent& evt)
{
	if (evt.reason == net::eActorLifecycleReason::Spawned && m_pendingPlayerSpawnReqIds.erase(evt.spawnReqId) > 0 && evt.isLocal)
	{
		m_localObjectId = evt.objectId;
		//SpawnPlayerDrone();
	}

	if ((evt.reason == net::eActorLifecycleReason::Despawned
		|| evt.reason == net::eActorLifecycleReason::PredictedDespawn)
		&& evt.objectId == m_localObjectId)
	{
		m_localObjectId = px::INVALID_OBJ_ID;
	}

	OnActorLifecycle(evt);
}

void ClientInstance::HandleClickMoveResolved(const net::ClickMoveResolvedEvent& evt)
{
	OnClickMoveResolved(evt);
}
