#include "pch.h"
#include "UserInstance.h"

#include <algorithm>
#include <cmath>
#include <ranges>

#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"

namespace
{
	const char* LifecycleReasonName(net::eRenderActorLifecycleReason reason)
	{
		switch (reason)
		{
		case net::eRenderActorLifecycleReason::Created: return "CREATE";
		case net::eRenderActorLifecycleReason::AoiEntered: return "AOI_ENTER";
		case net::eRenderActorLifecycleReason::AoiLeft: return "AOI_LEAVE";
		case net::eRenderActorLifecycleReason::Destroyed: return "DESTROY";
		case net::eRenderActorLifecycleReason::PredictedDespawn: return "PREDICTED_DESPAWN";
		default: return "UNKNOWN";
		}
	}

	glm::vec4 ResolveColor(
		px::eActorType actorType,
		px::eBodyType bodyType,
		px::eMotionType motionType,
		px::eCharacterControlType controllerType = px::eCharacterControlType::None)
	{
		struct ColorRule
		{
			px::eActorType              actorType;
			px::eBodyType               bodyType;
			px::eMotionType             motionType;
			px::eCharacterControlType   controllerType;
			bool                        checkController;
			glm::vec4                   color;
		};

		static constexpr ColorRule kTable[] =
		{
			{ px::eActorType::Generic,      px::eBodyType::Rigid,       px::eMotionType::Static,    px::eCharacterControlType::None,    false, { 0.55f, 0.55f, 0.55f, 1.0f } },
			{ px::eActorType::Generic,      px::eBodyType::Rigid,       px::eMotionType::Dynamic,   px::eCharacterControlType::None,    false, { 0.95f, 0.65f, 0.20f, 1.0f } },
			{ px::eActorType::Generic,      px::eBodyType::Rigid,       px::eMotionType::Kinematic, px::eCharacterControlType::None,    false, { 0.95f, 0.90f, 0.20f, 1.0f } },
			{ px::eActorType::Projectile,   px::eBodyType::Rigid,       px::eMotionType::Dynamic,   px::eCharacterControlType::None,    false, { 1.00f, 0.25f, 0.25f, 1.0f } },
			{ px::eActorType::Character,    px::eBodyType::Character,   px::eMotionType::CCT,       px::eCharacterControlType::Player,  true,  { 0.25f, 0.90f, 0.35f, 1.0f } },
			{ px::eActorType::Character,    px::eBodyType::Character,   px::eMotionType::CCT,       px::eCharacterControlType::AI,      true,  { 0.85f, 0.30f, 0.90f, 1.0f } },
			{ px::eActorType::Character,    px::eBodyType::Character,   px::eMotionType::RemoteCCT, px::eCharacterControlType::Player,  true,  { 0.20f, 0.75f, 1.00f, 1.0f } },
			{ px::eActorType::Character,    px::eBodyType::Character,   px::eMotionType::RemoteCCT, px::eCharacterControlType::AI,      true,  { 0.55f, 0.40f, 0.95f, 1.0f } },
		};

		for (const auto& rule : kTable)
		{
			if (rule.actorType != actorType || rule.bodyType != bodyType || rule.motionType != motionType)
				continue;

			if (rule.checkController && rule.controllerType != controllerType)
				continue;

			return rule.color;
		}

		return glm::vec4(0.80f, 0.80f, 0.80f, 1.0f);
	}
}

UserInstance::UserInstance(uint32 instanceId, uint64 userId)
	: ClientInstance(instanceId, userId)
{
	m_type = eClientType::User;
}

void UserInstance::Update(float deltaTime)
{
	ClientInstance::Update(deltaTime);

	if (!GetNetworkManager())
		return;

	static constexpr float TICK_INTERVAL = 1.0f / 60.0f;
	m_tickAccumulator += deltaTime;
	while (m_tickAccumulator >= TICK_INTERVAL)
	{
		m_currentRenderTick++;
		m_tickAccumulator -= TICK_INTERVAL;
	}

	CleanupOldSnapshots(m_latestServerTick);
}

void UserInstance::Render()
{
	if (!GetNetworkManager() || GetWindowIndex() >= MAX_WINDOWS)
		return;

	auto& renderer = Renderer::Instance();

	renderer.PreRender(GetWindowIndex());

	BuildRenderFrames();
	UpdateCamera();
	renderer.SetCameraLookAt(m_cameraPos, m_cameraTarget);
	renderer.SetPerspective(45.0f, 0.1f, 100.0f);

	RenderLevelMap();
	RenderActors();
	RenderClickMoveMarker();

	renderer.PostRender(GetWindowIndex());
}

void UserInstance::UpdateInput(float deltaTime)
{
	(void)deltaTime;
	ProcessControlInput();
}

void UserInstance::OnSpawnRequested(SpawnKind kind, uint32 spawnReqId)
{
	ActorRenderingData data{};
	data.ensured = false;
	data.pendingSpawnReqId = spawnReqId;

	switch (kind)
	{
	case SpawnKind::Player:
		data.shape = px::eShapeType::Capsule;
		data.capsuleRadius = 0.35f;
		data.capsuleHalfHeight = 0.5f;
		data.color = glm::vec4(0.9f, 0.25f, 0.25f, 1.0f);
		break;
	case SpawnKind::PlayerDrone:
		data.shape = px::eShapeType::Box;
		data.boxHalfExtents = px::Vec3{ 0.5f, 0.5f, 0.5f };
		data.color = glm::vec4(0.20f, 0.85f, 1.00f, 1.0f);
		break;
	case SpawnKind::Bullet:
		data.shape = px::eShapeType::Sphere;
		data.sphereRadius = 0.3f;
		data.color = glm::vec4(0.20f, 0.45f, 1.00f, 1.0f);
		break;
	default:
		break;
	}

	m_pendingSpawnToRenderData.emplace(spawnReqId, std::move(data));
}

void UserInstance::OnLevelSpawned(const net::RenderLevelSpawnedEvent& evt)
{
	CreateRenderingLevelData(evt);
}

void UserInstance::OnActorSpawned(const net::RenderActorSpawnedEvent& evt)
{
	if (auto it = m_pendingSpawnToRenderData.find(evt.spawnReqId); it != m_pendingSpawnToRenderData.end())
	{
		ActorRenderingData data = it->second;
		m_pendingSpawnToRenderData.erase(it);

		data.ensured = true;
		data.oid = evt.objectId;
		data.isLocal = evt.isLocal;

		JAMNET_LOG_INFO(
			"[AOI][ClientUser] {} user={} netId={} objectId={} local={} spawnReq={}",
			LifecycleReasonName(evt.reason),
			GetUserId(),
			evt.netId,
			data.oid,
			data.isLocal ? "yes" : "no",
			evt.spawnReqId);

		m_actorRenderData.emplace(evt.objectId, std::move(data));
		return;
	}

	ActorRenderingData* data = EnsureRenderingActorData(evt);
	if (!data) return;

	data->ensured = true;
	data->pendingSpawnReqId = 0;
	data->isLocal = evt.isLocal;

	JAMNET_LOG_INFO(
		"[AOI][ClientUser] {} user={} netId={} objectId={} local={} spawnReq={}",
		LifecycleReasonName(evt.reason),
		GetUserId(),
		evt.netId,
		data->oid,
		data->isLocal ? "yes" : "no",
		evt.spawnReqId);
}

void UserInstance::OnActorDespawned(const net::RenderActorDespawnedEvent& evt)
{
	m_actorRenderData.erase(evt.objectId);

	JAMNET_LOG_INFO(
		"[AOI][ClientUser] {} user={} netId={} objectId={}",
		LifecycleReasonName(evt.reason),
		GetUserId(),
		evt.netId,
		evt.objectId);
}

void UserInstance::OnRenderSamples(const net::RenderSamplesEvent& evt)
{
	m_latestServerTick = std::max(evt.tick, m_latestServerTick);

	for (const auto& actor : evt.actors)
	{
		ActorRenderingData* data = GetRenderingActorData(actor.objectId);
		if (!data) continue;

		data->isLocal = actor.isLocal;

		ActorSnapshot snapshot;
		snapshot.tick = evt.tick;
		snapshot.character = actor.cs;
		snapshot.characterRaw = actor.csRaw;
		snapshot.rigid = actor.rs;

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

void UserInstance::ProcessControlInput()
{
	GLFWwindow* window = Renderer::Instance().GetWindow(GetWindowIndex());
	if (!window || !glfwGetWindowAttrib(window, GLFW_FOCUSED))
		return;

	if (!m_mouseInitialized)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		m_mouseInitialized = true;
	}

	const bool leftMouseDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT));
	const bool triggerShoot = leftMouseDown & !m_prevLeftMouseDown;
	m_prevLeftMouseDown = leftMouseDown;

	if (triggerShoot) HandleShoot(window);

	HandleGroundPick(window);

	uint32 inputFlags = px::INPUT_NONE;

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)            inputFlags |= px::INPUT_BACKWARD;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)            inputFlags |= px::INPUT_FORWARD;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)            inputFlags |= px::INPUT_RIGHT;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)            inputFlags |= px::INPUT_LEFT;
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)        inputFlags |= px::INPUT_JUMP;
	if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)            inputFlags |= px::INPUT_PRONE;
	if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS)            inputFlags |= px::INPUT_CROUCH;
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)            inputFlags |= px::INPUT_DASH;
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)            inputFlags |= px::INPUT_RUN;
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)   inputFlags |= px::INPUT_SPRINT;

	constexpr uint32 movementFlags = px::INPUT_FORWARD | px::INPUT_BACKWARD | px::INPUT_LEFT | px::INPUT_RIGHT;
	if ((inputFlags & movementFlags) != 0)
	{
		if (!m_prevMovementInputActive || m_hasMoveTarget || m_followMoveActive)
			++m_controlEpoch;
		m_prevMovementInputActive = true;

		m_hasMoveTarget = false;
		m_followMoveActive = false;
		++m_clickPickSeq;
		if (auto* netMgr = GetNetworkManager())
		{
			if (auto* world = netMgr->GetWorld())
				world->SetLatestClickMoveSeq(m_clickPickSeq);
		}
		m_pitch = 0.0f;
		ControlCharacter(inputFlags, m_pitch, m_yaw, m_controlEpoch);
		return;
	}

	m_prevMovementInputActive = false;

	if (ApplyClickMoveControl())
		return;
	if (m_followMoveActive)
		return;

	ControlCharacter(inputFlags, 0.0f, m_yaw, m_controlEpoch);
}

void UserInstance::HandleShoot(GLFWwindow* window)
{
	double mouseX = 0.0;
	double mouseY = 0.0;

	glfwGetCursorPos(window, &mouseX, &mouseY);

	glm::vec3 rayOrigin{};
	glm::vec3 rayDir{};

	if (Renderer::Instance().ScreenPointToWorldRay(GetWindowIndex(), mouseX, mouseY, rayOrigin, rayDir))
	{
		px::Vec3 localPos{};
		if (!TryGetLocalActorPosition(localPos))
			localPos = px::Vec3(m_cameraTarget.x, m_cameraTarget.y, m_cameraTarget.z);

		const px::Vec3 ro = px::Vec3(rayOrigin.x, rayOrigin.y, rayOrigin.z);
		const px::Vec3 rd = px::Vec3(rayDir.x, rayDir.y, rayDir.z).GetNormalized();

		static constexpr float k_muzzleHeight = 0.9f;
		static constexpr float k_muzzleForwardOffset = 0.45f;

		const px::Vec3 baseMuzzle = localPos + px::Vec3(0.0f, k_muzzleHeight, 0.0f);

		px::Vec3 target = ro + rd * 50.0f;	// falllback
		if (std::abs(rd.y) > 1e-5f)
		{
			const float t = (baseMuzzle.y - ro.y) / rd.y;
			if (t > 0.0f)
				target = ro + rd * t;
		}

		px::Vec3 shootDir = (target - baseMuzzle).GetNormalized();
		if (shootDir.IsZero())
			shootDir = px::Vec3(0.0f, 0.0f, 1.0f);

		const px::Vec3 muzzlePos = baseMuzzle + shootDir * k_muzzleForwardOffset;
		SpawnBullet(muzzlePos, shootDir);
	}
}

void UserInstance::HandleGroundPick(GLFWwindow* window)
{
	if (!window) return;

	const bool rightMouseDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
	const bool triggerPick = rightMouseDown && !m_prevRightMouseDown;
	m_prevRightMouseDown = rightMouseDown;

	if (!triggerPick)
		return;

	double mouseX = 0.0;
	double mouseY = 0.0;
	glfwGetCursorPos(window, &mouseX, &mouseY);

	glm::vec3 rayOrigin{};
	glm::vec3 rayDir{};
	if (!Renderer::Instance().ScreenPointToWorldRay(GetWindowIndex(), mouseX, mouseY, rayOrigin, rayDir))
		return;

	auto* netMgr = GetNetworkManager();
	if (!netMgr) return;
	auto* world = netMgr->GetWorld();
	if (!world) return;

	const px::Vec3 from(rayOrigin.x, rayOrigin.y, rayOrigin.z);
	const px::Vec3 dir(rayDir.x, rayDir.y, rayDir.z);
	const uint64 pickSeq = ++m_clickPickSeq;
	++m_controlEpoch;
	m_prevMovementInputActive = false;
	world->RequestClickMove(from, dir, m_clickMoveRange, pickSeq, m_controlEpoch, m_yaw);
}

void UserInstance::OnClickMoveResolved(const net::ClickMoveResolvedEvent& evt)
{
	if (evt.requestSeq != m_clickPickSeq)
		return;

	m_moveTarget		= evt.targetPos;
	m_hasMoveTarget		= !evt.followTarget;
	m_followMoveActive	= evt.followTarget;
	m_hasControlSample	= false;
	m_lastControlNs		= 0;
	m_estControlSpeed	= 0.0f;
}

bool UserInstance::TryGetLocalActorPosition(OUT px::Vec3& outPos) const
{
	for (const auto& data : m_actorRenderData | views::values)
	{
		if (!data.isLocal || data.snapshots.empty())
			continue;

		const auto& latest = data.snapshots.back();
		if (latest.characterRaw.has_value())
		{
			outPos = latest.characterRaw->pos;
			return true;
		}
		if (latest.character.has_value())
		{
			outPos = latest.character->pos;
			return true;
		}
		if (latest.rigid.has_value())
		{
			outPos = latest.rigid->pose.p;
			return true;
		}
	}

	return false;
}

bool UserInstance::ApplyClickMoveControl()
{
	if (!m_hasMoveTarget)
		return false;

	px::Vec3 localPos{};
	if (!TryGetLocalActorPosition(localPos))
		return true;

	{
		const uint64 nowNs = NOW_NS();
		if (m_hasControlSample && nowNs > m_lastControlNs)
		{
			const float dt = std::clamp(static_cast<float>(nowNs - m_lastControlNs) * 1e-9f, 0.001f, 0.1f);
			const px::Vec3 planarDelta = px::Vec3(localPos.x - m_lastControlPos.x, 0.0f, localPos.z - m_lastControlPos.z);
			const float instSpeed = planarDelta.Magnitude() / dt;
			m_estControlSpeed = (m_estControlSpeed <= 0.0f)
				? instSpeed
				: (m_estControlSpeed * 0.65f + instSpeed * 0.35f);
		}

		m_lastControlPos = localPos;
		m_lastControlNs = nowNs;
		m_hasControlSample = true;
	}

	px::Vec3 delta = m_moveTarget - localPos;
	delta.y = 0.0f;
	const float distSq = delta.x * delta.x + delta.z * delta.z;
	const float dist = std::sqrt(distSq);
	const float brakingRadius = m_clickMoveStopRadius + m_estControlSpeed * m_clickMoveStopLeadTime;

	if (dist <= brakingRadius)
	{
		m_hasMoveTarget = false;
		m_followMoveActive = false;
		if (auto* netMgr = GetNetworkManager())
		{
			if (auto* world = netMgr->GetWorld())
			{
				px::CharacterInput stop{};
				stop.commandEpoch = m_controlEpoch;
				stop.moveMode = px::eMoveInputMode::Keyboard;
				stop.facingYaw = m_yaw;
				world->PushInput(stop);
			}
		}
		return true;
	}

	// mouse move mode input is already latched in ClientInputSystem;
	// we only keep this local check to decide when to emit stop.
	return true;
}

void UserInstance::CreateRenderingLevelData(const net::RenderLevelSpawnedEvent& evt)
{
	for (const auto& [objectId, prefab] : evt.instances)
	{
		auto [it, inserted] = m_actorRenderData.try_emplace(objectId, ActorRenderingData{});
		ActorRenderingData& data = it->second;
		data.oid = objectId;

		if (inserted)
		{
			const auto* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(prefab);
			if (!def || !def->IsRigid()) continue;

			const auto& rigidBody = std::get<px::RigidBodyDef>(def->body);
			const auto& shapeDef  = PHYSICS_PREFAB_REGISTRY.GetShapeDef(rigidBody.shapes[0]);

			if (shapeDef.IsMeshGeometry()) continue;

			data.ensured			= true;
			data.pendingSpawnReqId	= 0;
			data.isLocal			= false;
			data.shape				= shapeDef.type;
			data.boxHalfExtents	    = px::ToPx(shapeDef.halfExtents);
			data.capsuleRadius		= shapeDef.radius;
			data.capsuleHalfHeight	= shapeDef.halfHeight;
			data.sphereRadius		= shapeDef.radius;
			data.color				= ResolveColor(def->actorType, def->bodyType, def->motionType);

			data.isFloor			= def->name == "Floor";
		}
	}
}

ActorRenderingData* UserInstance::EnsureRenderingActorData(const net::RenderActorSpawnedEvent& evt)
{
	auto [it, inserted] = m_actorRenderData.try_emplace(evt.objectId, ActorRenderingData{});
	ActorRenderingData& data = it->second;
	data.oid = evt.objectId;

	if (inserted)
	{
		const auto* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(evt.prefab);
		if (!def) return nullptr;

		if (def->IsRigid())
		{
			const auto& rigidBody = std::get<px::RigidBodyDef>(def->body);
			const auto& shapeDef = PHYSICS_PREFAB_REGISTRY.GetShapeDef(rigidBody.shapes[0]);

			data.shape				= shapeDef.type;
			data.boxHalfExtents		= px::ToPx(shapeDef.halfExtents);
			data.capsuleRadius		= shapeDef.radius;
			data.capsuleHalfHeight	= shapeDef.halfHeight;
			data.sphereRadius		= shapeDef.radius;

			data.color = ResolveColor(def->actorType, def->bodyType, def->motionType);
		}
		else
		{
			const auto& charBody = std::get<px::CharacterBodyDef>(def->body);
			const auto& cct = PHYSICS_PREFAB_REGISTRY.GetCCTBodyDef(charBody.cct);

			data.shape				= px::eShapeType::Capsule;
			data.capsuleHalfHeight	= cct.height * 0.5f;
			data.capsuleRadius		= cct.radius;

			data.color = ResolveColor(def->actorType, def->bodyType, def->motionType, charBody.controllerType);
		}
	}

	return &data;
}

void UserInstance::BuildRenderFrames()
{
	static constexpr float kTickInterval = 1.0f / 60.0f;
	const float tickAlpha = std::clamp(m_tickAccumulator / kTickInterval, 0.0f, 1.0f);
	const float renderTick = (m_latestServerTick > INTERPOLATION_DELAY)
		? static_cast<float>(m_latestServerTick - INTERPOLATION_DELAY) + tickAlpha : tickAlpha;

	for (auto& data : m_actorRenderData | views::values)
	{
		data.renderFrameCached = false;

		if (!data.ensured || data.snapshots.empty())
			continue;

		px::Vec3 interpPos;
		px::Quat interpRot;
		InterpolateActorTransform(data, renderTick, interpPos, interpRot);
		GetSmoothedLocalTransform(data, interpPos, interpRot, data.cachedRenderPos, data.cachedRenderRot);

		data.renderFrameCached = true;
	}
}

void UserInstance::UpdateCamera()
{
	for (auto& data : m_actorRenderData | views::values)
	{
		if (!data.isLocal || !data.renderFrameCached)
			continue;

		glm::vec3 actorPos(data.cachedRenderPos.x, data.cachedRenderPos.y, data.cachedRenderPos.z);

		if (std::abs(actorPos.y - m_lastActorY) < m_actorYStabilizeEps)
			actorPos.y = m_lastActorY;
		else
			m_lastActorY = actorPos.y;

		const glm::vec3 offset(0.0f, m_topDownHeight, -m_topDownBackOffset);

		m_cameraPos    = actorPos + offset;
		m_cameraTarget = actorPos + glm::vec3(0.0f, 0.5f, 0.0f);
		return;
	}

	m_cameraPos		= glm::vec3(0, 24, -12);
	m_cameraTarget	= glm::vec3(0, 0, 0);
}

void UserInstance::RenderClickMoveMarker()
{
	if (!m_hasMoveTarget)
		return;

	const glm::vec3 center(m_moveTarget.x, m_moveTarget.y + 0.05f, m_moveTarget.z);
	const glm::vec4 color(1.0f, 0.9f, 0.1f, 1.0f);
	const float markerHalf = 0.35f;

	Renderer::Instance().DrawRay(
		center + glm::vec3(-markerHalf, 0.0f, 0.0f),
		center + glm::vec3( markerHalf, 0.0f, 0.0f),
		color);
	Renderer::Instance().DrawRay(
		center + glm::vec3(0.0f, 0.0f, -markerHalf),
		center + glm::vec3(0.0f, 0.0f,  markerHalf),
		color);
	Renderer::Instance().DrawRay(
		center,
		center + glm::vec3(0.0f, 0.35f, 0.0f),
		color);
}

void UserInstance::RenderActors()
{
	for (auto& data : m_actorRenderData | views::values)
	{
		if (!data.ensured || !data.renderFrameCached)
			continue;

		glm::vec3 position(data.cachedRenderPos.x, data.cachedRenderPos.y, data.cachedRenderPos.z);
		glm::vec3 rotation = QuatToEuler(data.cachedRenderRot);

		switch (data.shape)
		{
		case px::eShapeType::Box:
		{
			glm::vec3 scale = glm::vec3(data.boxHalfExtents.x * 2.f, data.boxHalfExtents.y * 2.f, data.boxHalfExtents.z * 2.f);
			
			if (data.isFloor)
				Renderer::Instance().DrawGridPlaneBox(position, rotation, scale, data.color, glm::vec4(1.f, 0.f, 0.f, 1.f), 5);
			else
				Renderer::Instance().DrawBox(position, rotation, scale, data.color);
			break;
		}

		case px::eShapeType::Sphere:
		{
			Renderer::Instance().DrawSphere(position, data.sphereRadius, data.color);
			break;
		}
		case px::eShapeType::Capsule:
		{
			Renderer::Instance().DrawCapsule(position, data.capsuleRadius, data.capsuleHalfHeight, data.color);
			break;
		}
		case px::eShapeType::Plane:
		{
			Renderer::Instance().DrawPlane(position, glm::vec2(WORLD_RANGE_MAX, WORLD_RANGE_MAX), data.color);
			break;
		}

		default:
			break;
		}
	}
}

void UserInstance::RenderLevelMap()
{
	Renderer::Instance().DrawLoadedGLTF();
}

void UserInstance::GetSmoothedLocalTransform(ActorRenderingData& data, const px::Vec3& targetPos, const px::Quat& targetRot, OUT px::Vec3& outPos, OUT px::Quat& outRot)
{
	if (!data.hasSmoothed)
	{
		data.smoothedPos = targetPos;
		data.smoothedRot = targetRot;
		data.hasSmoothed = true;
	}
	else
	{
		static constexpr float kPosAlpha = 0.30f;
		static constexpr float kRotAlpha = 0.25f;

		data.smoothedPos = data.smoothedPos + (targetPos - data.smoothedPos) * kPosAlpha;
		data.smoothedRot = px::Quat::Lerp(data.smoothedRot, targetRot, kRotAlpha);
	}

	data.lastSmoothedFrame = m_currentRenderTick;

	outPos = data.smoothedPos;
	outRot = data.smoothedRot;
}

glm::vec3 UserInstance::QuatToEuler(const px::Quat& q) const
{
	glm::vec3 euler;

	float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
	float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
	euler.x = std::atan2(sinr_cosp, cosr_cosp);

	float sinp = 2.0f * (q.w * q.y - q.z * q.x);
	if (std::abs(sinp) >= 1.0f)
		euler.y = std::copysign(physx::PxPi / 2.0f, sinp);
	else
		euler.y = std::asin(sinp);

	float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
	float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
	euler.z = std::atan2(siny_cosp, cosy_cosp);

	return euler;
}

ActorRenderingData* UserInstance::GetRenderingActorData(px::ObjectId id)
{
	auto it = m_actorRenderData.find(id);
	if (it != m_actorRenderData.end())
	{
		return &it->second;
	}

	return nullptr;
}

void UserInstance::InterpolateActorTransform(const ActorRenderingData& data, float renderTick, OUT px::Vec3& pos, OUT px::Quat& rot) const
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
		if (static_cast<float>(snapshot.tick) <= renderTick)
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

	const float prevTickF = static_cast<float>(prevSnapshot->tick);
	const float nextTickF = static_cast<float>(nextSnapshot->tick);
	const float tickDelta = nextTickF - prevTickF;
	if (tickDelta <= 0.0f)
	{
		GetPosAndRot(prevSnapshot, pos, rot);
		return;
	}

	float t = (renderTick - prevTickF) / tickDelta;
	t = std::clamp(t, 0.0f, 1.0f);
	t = t * t * (3 - 2 * t);

	px::Vec3 prevPos, nextPos;
	px::Quat prevRot, nextRot;

	GetPosAndRot(prevSnapshot, prevPos, prevRot);
	GetPosAndRot(nextSnapshot, nextPos, nextRot);

	pos = prevPos + (nextPos - prevPos) * t;
	rot = px::Quat::Lerp(prevRot, nextRot, t);
}

void UserInstance::CleanupOldSnapshots(uint32 currentTick)
{
	constexpr uint32 MAX_TICK_AGE = 100;

	for (auto& data : m_actorRenderData | views::values)
	{
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
