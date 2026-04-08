#include "pch.h"
#include "ClientInstance.h"

#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jampx/PhysicsFacade.h"

ClientInstance::ClientInstance(uint32 instanceId, uint64 userId)
	: m_instanceId(instanceId), m_userId(userId)
{
	m_subLevelSpawned = GLOBAL_EVENTBUS_SUBSCRIBE(
		jam::net::RenderLevelSpawnedEvent,
		[this](const jam::net::RenderLevelSpawnedEvent& evt) { HandleLevelSpawned(evt); },
		jam::SubscribeOptions{ jam::eDispatchPolicy::MainExecutor }
	);

	m_subActorSpawned = GLOBAL_EVENTBUS_SUBSCRIBE(
		jam::net::RenderActorSpawnedEvent,
		[this](const jam::net::RenderActorSpawnedEvent& evt) { HandleActorSpawned(evt); },
		jam::SubscribeOptions{ jam::eDispatchPolicy::MainExecutor }
	);

	m_subActorDespawned = GLOBAL_EVENTBUS_SUBSCRIBE(
		jam::net::RenderActorDespawnedEvent,
		[this](const jam::net::RenderActorDespawnedEvent& evt) { HandleActorDespawned(evt); },
		jam::SubscribeOptions{ jam::eDispatchPolicy::MainExecutor }
	);

	m_subRenderSamples = GLOBAL_EVENTBUS_SUBSCRIBE(
		jam::net::RenderSamplesEvent,
		[this](const jam::net::RenderSamplesEvent& evt) { HandleRenderSamples(evt); },
		jam::SubscribeOptions{ jam::eDispatchPolicy::MainExecutor }
	);

	JAMNET_LOG_INFO("[Client #{}] Created", m_instanceId);
}

ClientInstance::~ClientInstance()
{
	GLOBAL_EVENTBUS_UNSUBSCRIBE(m_subLevelSpawned.type, m_subLevelSpawned.id);
	GLOBAL_EVENTBUS_UNSUBSCRIBE(m_subActorSpawned.type, m_subActorSpawned.id);
	GLOBAL_EVENTBUS_UNSUBSCRIBE(m_subActorDespawned.type, m_subActorDespawned.id);
	GLOBAL_EVENTBUS_UNSUBSCRIBE(m_subRenderSamples.type, m_subRenderSamples.id);

	Disconnect();
	JAMNET_LOG_INFO("[Client #{}] Destroyed", m_instanceId);
}

bool ClientInstance::Connect(const string& serverIp, uint16 tcpPort, uint16 udpPort)
{
	if (m_networkManager)
	{
		JAMNET_LOG_WARN("[Client #{}] Already connected", m_instanceId);
		return true;
	}

	net::ClientConfig config{};
	config.serverTcpAddress = net::NetAddress(serverIp, tcpPort);
	config.serverUdpAddress = net::NetAddress(serverIp, udpPort);
	config.physicsFactory = [] { return std::make_unique<jam::px::PhysicsFacade>(); };
	config.levelPath = "C://Users//akxotjr//GameWorkSpace//Jam//TestApp//Contents//test_level1.json";

	m_networkManager = std::make_unique<net::ClientNetworkManager>(config, m_userId);

	if (!m_networkManager->Connect())
	{
		JAMNET_LOG_ERROR("[Client #{}] Failed to try connecting to server", m_instanceId);
		m_networkManager.reset();
		return false;
	}

	JAMNET_LOG_INFO("[Client #{}] success to try connecting to server (userId= {})", m_instanceId, m_userId);
	return true;
}

void ClientInstance::Disconnect()
{
	if (!m_networkManager)
		return;

	m_networkManager->Disconnect();
	m_networkManager.reset();

	m_pendingPlayerSpawnReqIds.clear();
	m_localObjectId = px::INVALID_OBJ_ID;

	JAMNET_LOG_INFO("[Client #{}] Disconnected", m_instanceId);
}

bool ClientInstance::IsConnected() const
{
	return m_networkManager && m_networkManager->IsConnected();
}

void ClientInstance::Update(float deltaTime)
{
	if (!m_networkManager)
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
	if (!m_networkManager) return;

	auto* world = m_networkManager->GetWorld();
	if (!world) return;

	net::SpawnParams charParams{};
	charParams.spawnId		  = m_nextSpawnReqId++;
	charParams.owned		  = true;
	charParams.controlled	  = true;

	charParams.desc.prefab    = px::MakePrefabKey("Character");
	charParams.desc.pose	  = { .p = { 15.0f * static_cast<float>(m_windowIndex), 10.f, 0.f } };
	charParams.desc.overrides = px::CharacterSpawnOverrides{};

	m_pendingPlayerSpawnReqIds.insert(charParams.spawnId);
	OnSpawnRequested(SpawnKind::Player, charParams.spawnId);
	world->SpawnActor(charParams);
}

void ClientInstance::SpawnPlayerDrone()
{
	if (!m_networkManager) return;

	auto* world = m_networkManager->GetWorld();
	if (!world) return;

	net::SpawnParams droneParams{};
	droneParams.spawnId			= m_nextSpawnReqId++;
	droneParams.owned			= true;
	droneParams.controlled		= false;
	droneParams.targetObjectId	= m_localObjectId;

	droneParams.desc.prefab		= px::MakePrefabKey("OrbitDrone");
	droneParams.desc.spawnSrc	= px::eSpawnSource::Runtime;
	droneParams.desc.pose		= {};
	droneParams.desc.overrides	= px::RigidSpawnOverrides{};

	OnSpawnRequested(SpawnKind::PlayerDrone, droneParams.spawnId);
	world->SpawnActor(droneParams);
}

void ClientInstance::SpawnBullet()
{
	if (!m_networkManager) return;

	auto* world = m_networkManager->GetWorld();
	if (!world) return;

	net::SpawnParams bulletParams{};
	bulletParams.spawnId		= m_nextSpawnReqId++;
	bulletParams.owned			= true;
	bulletParams.controlled		= false;
	bulletParams.targetObjectId = m_localObjectId;

	bulletParams.desc.prefab	= px::MakePrefabKey("LinearProjectile");
	bulletParams.desc.spawnSrc	= px::eSpawnSource::Runtime;
	bulletParams.desc.pose		= { .p = { 0.0, 0.75, 0.5 } };
	bulletParams.desc.overrides = px::RigidSpawnOverrides{
		.mask = px::SpawnOverrideMask::LINEAR_VEL,
		.linearVelocity = px::Vec3(0.0, 1.0, 10.0)
	};

	OnSpawnRequested(SpawnKind::Bullet, bulletParams.spawnId);
	world->SpawnActor(bulletParams);
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

void ClientInstance::ControlCharacter(uint32 inputFlags, float pitch, float yaw)
{
	if (!m_networkManager) return;

	auto* world = m_networkManager->GetWorld();
	if (!world) return;

	world->PushInput(inputFlags, yaw, pitch);
}

void ClientInstance::HandleLevelSpawned(const net::RenderLevelSpawnedEvent& evt)
{
	if (evt.userId != m_userId)
		return;

	OnLevelSpawned(evt);
}

void ClientInstance::HandleActorSpawned(const net::RenderActorSpawnedEvent& evt)
{
	if (evt.userId != m_userId)
		return;

	if (m_pendingPlayerSpawnReqIds.erase(evt.spawnReqId) > 0 && evt.isLocal)
	{
		m_localObjectId = evt.objectId;
		SpawnPlayerDrone();
	}

	OnActorSpawned(evt);
}

void ClientInstance::HandleActorDespawned(const net::RenderActorDespawnedEvent& evt)
{
	if (evt.userId != m_userId)
		return;

	if (evt.objectId == m_localObjectId)
		m_localObjectId = px::INVALID_OBJ_ID;

	OnActorDespawned(evt);
}

void ClientInstance::HandleRenderSamples(const net::RenderSamplesEvent& evt)
{
	if (evt.userId != m_userId)
		return;

	OnRenderSamples(evt);
}
