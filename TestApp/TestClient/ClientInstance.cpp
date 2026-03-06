#include "pch.h"
#include "ClientInstance.h"

#include <algorithm>
#include <filesystem>

#include "Renderer.h"
#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jampx/PhysicsFacade.h"


ClientInstance::ClientInstance(uint32 instanceId, uint64 userId)
	: m_instanceId(instanceId), m_userId(userId)
{
	m_subActorSpawned = GLOBAL_EVENTBUS_SUBSCRIBE(
		jam::net::RenderActorSpawnedEvent,
		[this](const jam::net::RenderActorSpawnedEvent& evt) { OnActorSpawned(evt); },
		jam::SubscribeOptions{ jam::eDispatchPolicy::MAIN_EXECUTOR }
	);

	// 액터 제거 이벤트 구독
	m_subActorDespawned = GLOBAL_EVENTBUS_SUBSCRIBE(
		jam::net::RenderActorDespawnedEvent,
		[this](const jam::net::RenderActorDespawnedEvent& evt) { OnActorDespawned(evt); },
		jam::SubscribeOptions{ jam::eDispatchPolicy::MAIN_EXECUTOR }
	);


	m_subRenderSamples = GLOBAL_EVENTBUS_SUBSCRIBE(jam::net::RenderSamplesEvent,
		[this](const jam::net::RenderSamplesEvent& evt) { OnRenderSamples(evt); }, 
		jam::SubscribeOptions{ jam::eDispatchPolicy::MAIN_EXECUTOR });


	JAMNET_LOG_INFO("[Client #{}] Created", m_instanceId);
}

ClientInstance::~ClientInstance()
{
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

	m_spawnedActorNetIds.clear();  
	m_controllableActorNetId = 0;

	net::ClientConfig config{};
	config.serverTcpAddress = net::NetAddress(serverIp, tcpPort);
	config.serverUdpAddress = net::NetAddress(serverIp, udpPort);
	config.physicsFactory	= [] { return std::make_unique<jam::px::PhysicsFacade>(); };
	config.levelPath		= "C://Users//akxotjr//GameWorkSpace//Jam//TestApp//TestClient//Contents//test_level.json";

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

	m_spawnedActorNetIds.clear();
	m_controllableActorNetId = 0;
	m_actorRenderData.clear();

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

	ProcessControlInput();

	static float tickAccumulator = 0.0f;
	static constexpr float TICK_INTERVAL = 1.0f / 60.0f;

	tickAccumulator += deltaTime;
	while (tickAccumulator >= TICK_INTERVAL)
	{
		m_currentRenderTick++;
		tickAccumulator -= TICK_INTERVAL;
	}

	CleanupOldSnapshots(m_latestServerTick);
}

void ClientInstance::Render()
{
	if (!m_networkManager || m_windowIndex >= MAX_WINDOWS)
		return;

	auto& renderer = Renderer::Instance();

	renderer.PreRender(m_windowIndex);

	BuildRenderFrames();
	UpdateCamera();
	renderer.SetCameraLookAt(m_cameraPos, m_cameraTarget);
	renderer.SetPerspective(45.0f, 0.1f, 100.0f);

	RenderLevelMap();
	RenderActors();

	renderer.PostRender(m_windowIndex);
}

void ClientInstance::SpawnActor()
{
	if (!m_networkManager) return;

	auto* world = m_networkManager->GetWorld();
	if (!world)
		return;

	net::SpawnParams params{};
	params.spawnId		= m_nextSpawnReqId++;
	params.owned		= true;
	params.controlled	= true;

	params.desc.prefab = px::MakePrefabKey("Character");
	params.desc.cs = { .pos = { 5.0f * static_cast<float>(m_windowIndex), 10.f, 0.f} };

	{
		ActorRenderingData data{};
		data.ensured			= false;
		data.pendingSpawnReqId	= params.spawnId;

		data.shape				= px::prefab::eShapeType::CAPSULE;
		data.capsuleRadius		= 0.35f;
		data.capsuleHalfHeight	= 0.5f;
		data.color				= glm::vec4(0.9f, 0.25f, 0.25f, 1.0f);

		m_pendingSpawnToRenderData.emplace(params.spawnId, data);
	}
	world->SpawnActor(params);
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


void ClientInstance::ProcessControlInput()
{
	GLFWwindow* window = Renderer::Instance().GetWindow(m_windowIndex);
	if (!window && !glfwGetWindowAttrib(window, GLFW_FOCUSED))
		return;

	uint32 inputFlags = px::INPUT_NONE;

	// 마우스 캡처 토글: 좌클릭으로 캡처, ESC로 해제
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
	{
		if (!m_mouseCaptured)
		{
			m_mouseCaptured = true;
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			m_mouseInitialized = false; // 다음 프레임부터 상대 이동량 시작
		}
	}
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
	{
		if (m_mouseCaptured)
		{
			m_mouseCaptured = false;
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)			inputFlags |= px::INPUT_BACKWARD;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)			inputFlags |= px::INPUT_FORWARD;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)			inputFlags |= px::INPUT_RIGHT;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)			inputFlags |= px::INPUT_LEFT;
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)		inputFlags |= px::INPUT_JUMP;
	if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)			inputFlags |= px::INPUT_PRONE;
	if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS)			inputFlags |= px::INPUT_CROUCH;
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)			inputFlags |= px::INPUT_DASH;
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)			inputFlags |= px::INPUT_RUN;
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)	inputFlags |= px::INPUT_SPRINT;

	if (m_mouseCaptured)
	{
		ProcessMouseLook(window);
	}

	ControlCharacter(inputFlags, m_pitch, m_yaw);
}


void ClientInstance::ProcessMouseLook(GLFWwindow* window)
{
    if (!window) return;

    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (!m_mouseInitialized)
    {
        m_lastMouseX		= xpos;
        m_lastMouseY		= ypos;
        m_mouseInitialized	= true;
        return;
    }

    double dx = xpos - m_lastMouseX;
    double dy = ypos - m_lastMouseY;
    m_lastMouseX = xpos;
    m_lastMouseY = ypos;

    m_yaw   += static_cast<float>(dx) * m_mouseSensitivity;
    m_pitch -= static_cast<float>(dy) * m_mouseSensitivity;

    const float maxPitch = PxPi * 0.5f * 0.94f;
    m_pitch = std::min(m_pitch, maxPitch);
    m_pitch = std::max(m_pitch, -maxPitch);

    //// 카메라 기준 XZ forward/right 계산
    //const float cy = cosf(m_yaw);
    //const float sy = sinf(m_yaw);

    //m_viewForwardXZ = glm::vec2(sy, cy);     // forward: (x,z) = (sy, cy)
    //m_viewRightXZ   = glm::vec2(cy, -sy);    // right: (x,z) = (cy, -sy)
}


ActorRenderingData* ClientInstance::EnsureRenderingActorData(px::ObjectKey key)
{
	auto [it, inserted] = m_actorRenderData.try_emplace(key, ActorRenderingData{});
	ActorRenderingData& data = it->second;
	data.key = key;

	if (inserted)
	{
		data.color = glm::vec4(0.9f, 0.25f, 0.25f, 1.0f);
		data.shape = px::prefab::eShapeType::CAPSULE;
		data.capsuleRadius = 0.35f;
		data.capsuleHalfHeight = 0.45f;
	}

	return &data;
}

void ClientInstance::BuildRenderFrames()
{
	const uint32 renderTick = (m_latestServerTick > INTERPOLATION_DELAY) ? (m_latestServerTick - INTERPOLATION_DELAY) : 0;

	for (auto& data : m_actorRenderData | views::values)
	{
		data.renderFrameCached = false;

		if (!data.ensured || data.snapshots.empty())
			continue;

		px::Vec3 interpPos;
		px::Quat interpRot;
		InterpolateActorTransform(data, renderTick, interpPos, interpRot);

		if (data.isLocal)
		{
			GetSmoothedLocalTransform(data, interpPos, interpRot,
				data.cachedRenderPos, data.cachedRenderRot);
		}
		else
		{
			data.cachedRenderPos = interpPos;
			data.cachedRenderRot = interpRot;
		}

		data.renderFrameCached = true;
	}
}


void ClientInstance::OnActorSpawned(const net::RenderActorSpawnedEvent& evt)
{	
	if (evt.userId != m_userId)
		return;

	if (auto it = m_pendingSpawnToRenderData.find(evt.spawnReqId); it != m_pendingSpawnToRenderData.end())
	{
		ActorRenderingData data = it->second;
		m_pendingSpawnToRenderData.erase(it);

		data.ensured = true;
		data.key	 = evt.objectId;
		data.isLocal = evt.isLocal;

		m_actorRenderData.emplace(evt.objectId, std::move(data));
		return;
	}

	ActorRenderingData* data = EnsureRenderingActorData(evt.objectId);
	data->ensured			= true;
	data->pendingSpawnReqId = 0;
	data->isLocal			= evt.isLocal;
	data->color				= glm::vec4(0.25f, 0.55f, 0.95f, 1.0f);
}

void ClientInstance::OnActorDespawned(const net::RenderActorDespawnedEvent& evt)
{
	if (evt.userId != m_userId)
		return;

	m_actorRenderData.erase(evt.objectId);
}

void ClientInstance::OnRenderSamples(const net::RenderSamplesEvent& evt)
{
	if (evt.userId != m_userId)
		return;

	m_latestServerTick = std::max(evt.tick, m_latestServerTick);

	for (const auto& actor : evt.actors)
	{
		ActorRenderingData* data = GetRenderingActorData(actor.objectId);
		if (!data) return;

		// isLocal은 소유권 변경 가능하므로 매 프레임 업데이트
		data->isLocal = actor.isLocal;

		ActorSnapshot snapshot;
		snapshot.tick		= evt.tick;
		snapshot.character	= actor.cs;
		snapshot.rigid		= actor.rs;

		if (!data->snapshots.empty() && data->snapshots.back().tick == evt.tick)
		{
			data->snapshots.back() = snapshot;
		}
		else
		{
			data->snapshots.push_back(snapshot);

			if (data->snapshots.size() > MAX_SNAPSHOT_BUFFER)
			{
				data->snapshots.pop_front();
			}
		}
	}
}

void ClientInstance::UpdateCamera()
{
	for (auto& data : m_actorRenderData | views::values)
	{
		if (!data.isLocal || !data.renderFrameCached)
			continue;

		glm::vec3 actorPos(data.cachedRenderPos.x, data.cachedRenderPos.y, data.cachedRenderPos.z);

		// Y 안정화: 작은 변동 무시
		if (std::abs(actorPos.y - m_lastActorY) < m_actorYStabilizeEps)
			actorPos.y = m_lastActorY;
		else
			m_lastActorY = actorPos.y;

		constexpr float camDistance = 13.0f;
		constexpr float camHeight   = 7.0f;
		glm::vec3 offset;
		offset.x = camDistance * sinf(m_yaw);
		offset.y = camHeight;
		offset.z = camDistance * cosf(m_yaw);

		m_cameraPos = actorPos + offset;
		m_cameraTarget = actorPos;
		return;
	}

	// 로컬 액터 없음
	m_cameraPos = glm::vec3(0, 10, 20);
	glm::vec3 forward;
	forward.x = cosf(m_pitch) * sinf(m_yaw);
	forward.y = 0.0f;
	forward.z = cosf(m_pitch) * cosf(m_yaw);
	m_cameraTarget = glm::vec3(0, 0, 0) + forward;
}

void ClientInstance::RenderActors()
{
	for (auto& data : m_actorRenderData | views::values)
	{
		if (!data.ensured || !data.renderFrameCached)
			continue;

		glm::vec3 position(data.cachedRenderPos.x, data.cachedRenderPos.y, data.cachedRenderPos.z);
		glm::vec3 rotation = QuatToEuler(data.cachedRenderRot);

		//JAMNET_LOG_DEBUG("Render pos({}, {} ,{})", position.x, position.y, position.z);

		switch (data.shape)
		{
		case px::prefab::eShapeType::BOX:
		{
			glm::vec3 scale = glm::vec3(data.boxHalfExtents.x * 2.f, data.boxHalfExtents.y * 2.f, data.boxHalfExtents.z * 2.f);
			Renderer::Instance().DrawBox(position, rotation, scale, data.color);
			break;
		}

		case px::prefab::eShapeType::SPHERE:
		{
			Renderer::Instance().DrawSphere(position, data.sphereRadius, data.color);
			break;
		}
		case px::prefab::eShapeType::CAPSULE:
		{
			Renderer::Instance().DrawCapsule(position, data.capsuleRadius, data.capsuleHalfHeight, data.color);
			break;
		}
		case px::prefab::eShapeType::PLANE:
		{
			Renderer::Instance().DrawPlane(position, glm::vec2(WORLD_RANGE_MAX, WORLD_RANGE_MAX), data.color);
			break;
		}

		default:
			break;
		}
	}
}

void ClientInstance::RenderLevelMap()
{
	Renderer::Instance().DrawLoadedGLTF();
}

void ClientInstance::GetSmoothedLocalTransform(ActorRenderingData& data, const px::Vec3& targetPos, const px::Quat& targetRot, OUT px::Vec3& outPos, OUT px::Quat& outRot)
{
	// 스무딩 완전 비활성화: 항상 타겟에 스냅
	data.smoothedPos		= targetPos;
	data.smoothedRot		= targetRot;
	data.hasSmoothed		= true;
	data.lastSmoothedFrame	= m_currentRenderTick;

	outPos = data.smoothedPos;
	outRot = data.smoothedRot;
}

glm::vec3 ClientInstance::QuatToEuler(const px::Quat& q) const
{
	glm::vec3 euler;

	// Roll (x-axis rotation)
	float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
	float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
	euler.x = std::atan2(sinr_cosp, cosr_cosp);

	// Pitch (y-axis rotation)
	float sinp = 2.0f * (q.w * q.y - q.z * q.x);
	if (std::abs(sinp) >= 1.0f)
		euler.y = std::copysign(PxPi / 2.0f, sinp);
	else
		euler.y = std::asin(sinp);

	// Yaw (z-axis rotation)
	float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
	float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
	euler.z = std::atan2(siny_cosp, cosy_cosp);

	return euler;
}

ActorRenderingData* ClientInstance::GetRenderingActorData(px::ObjectKey key)
{
	auto it = m_actorRenderData.find(key);
	if (it != m_actorRenderData.end())
	{
		return &it->second;
	}

	return nullptr;
}

void ClientInstance::InterpolateActorTransform(const ActorRenderingData& data, uint32 renderTick, OUT px::Vec3& pos, OUT px::Quat& rot) const
{
	if (data.snapshots.empty())
	{
		pos = px::Vec3::Zero();
		rot = px::Quat::Identity();
		return;
	}

	auto GetPosAndRot = [&](const ActorSnapshot* snapshot, px::Vec3& p, px::Quat& q)
		{
			if (snapshot->character.has_value())
			{
				const auto& cs = snapshot->character.value();
				p = cs.pos;
				q = px::Quat::FromYawPitch(cs.facingYaw, cs.facingPitch);
			}
			else if (snapshot->rigid.has_value())
			{
				const auto& rs = snapshot->rigid.value();
				p = rs.pose.p;
				q = rs.pose.q;
			}
		};

	if (data.snapshots.size() == 1)
	{
		GetPosAndRot(&data.snapshots.front(), pos, rot);
		return;
	}


	const ActorSnapshot* prevSnapshot = nullptr;
	const ActorSnapshot* nextSnapshot = nullptr;

	for (const auto& snapshot : data.snapshots)
	{
		if (snapshot.tick <= renderTick)
		{
			prevSnapshot = &snapshot;
		}
		else
		{
			nextSnapshot = &snapshot;
			break;
		}
	}

	if (!prevSnapshot && !nextSnapshot)
	{
		GetPosAndRot(&data.snapshots.back(), pos, rot);
		return;
	}

	if (!prevSnapshot)
	{
		GetPosAndRot(nextSnapshot, pos, rot);
		return;
	}

	if (!nextSnapshot)
	{
		GetPosAndRot(prevSnapshot, pos, rot);
		return;
	}

	uint32 tickDelta = nextSnapshot->tick - prevSnapshot->tick;
	if (tickDelta == 0)
	{
		GetPosAndRot(prevSnapshot, pos, rot);
		return;
	}

	float t = static_cast<float>(renderTick - prevSnapshot->tick) / static_cast<float>(tickDelta);
	t = std::clamp(t, 0.0f, 1.0f);       // 먼저 0~1로 제한
	t = t * t * (3 - 2 * t);								// 그 다음 smoothstep

	// Position LERP

	px::Vec3 prevPos, nextPos;
	px::Quat prevRot, nextRot;

	GetPosAndRot(prevSnapshot, prevPos, prevRot);
	GetPosAndRot(nextSnapshot, nextPos, nextRot);

	pos = prevPos + (nextPos - prevPos) * t;
	rot = px::Quat::Lerp(prevRot, nextRot, t);
}

void ClientInstance::CleanupOldSnapshots(uint32 currentTick)
{
    constexpr uint32 MAX_TICK_AGE = 100;

    for (auto& data : m_actorRenderData | views::values)
    {
        // 정적 액터(업데이트 안 오는 경우 포함)를 위해 최소 1개 스냅샷은 유지
        while (data.snapshots.size() > 1)
        {
            if (currentTick > data.snapshots.front().tick + MAX_TICK_AGE)
            {
                data.snapshots.pop_front();
            }
            else
            {
                break;
            }
        }
    }
}