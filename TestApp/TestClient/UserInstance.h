#pragma once

#include "ClientInstance.h"

#include "Renderer.h"

#include "jampx/PhysicsAsset.h"

using namespace std;

struct ActorSnapshot
{
	uint32                          tick = 0;
	optional<px::RigidState>        rigid = nullopt;
	optional<px::CharacterState>    character = nullopt;
	optional<px::CharacterState>    characterRaw = nullopt;
};

struct ActorRenderingData
{
	glm::vec4               color				= {};
	px::eShapeType          shape				= {};
	px::Vec3                boxHalfExtents		= { 0.5f, 0.5f, 0.5f };
	float                   sphereRadius		= 0.5f;
	float                   capsuleRadius		= 0.5f;
	float                   capsuleHalfHeight	= 0.5f;
	string                  meshPath;

	bool					isFloor				= false;

	bool                    ensured				= false;
	uint32                  pendingSpawnReqId	= 0;

	px::ObjectId            oid					= px::INVALID_OBJ_ID;
	bool                    isLocal				= false;
	deque<ActorSnapshot>    snapshots;

	px::Vec3                smoothedPos			= px::Vec3::Zero();
	px::Quat                smoothedRot			= px::Quat::Identity();
	bool                    hasSmoothed			= false;
	uint32                  lastSmoothedFrame	= 0;

	px::Vec3                cachedRenderPos		= px::Vec3::Zero();
	px::Quat                cachedRenderRot		= px::Quat::Identity();
	bool                    renderFrameCached	= false;
};

class UserInstance : public ClientInstance
{
public:
	UserInstance(uint32 instanceId, uint64 userId);
	~UserInstance() override = default;

	void                    Update(float deltaTime) override;
	void                    Render() override;

protected:
	void                    UpdateInput(float deltaTime) override;
	void                    OnSpawnRequested(SpawnKind kind, uint32 spawnReqId) override;
	void                    OnLevelSpawned(const net::RenderLevelSpawnedEvent& evt) override;
	void                    OnActorSpawned(const net::RenderActorSpawnedEvent& evt) override;
	void                    OnActorDespawned(const net::RenderActorDespawnedEvent& evt) override;
	void                    OnClickMoveResolved(const net::ClickMoveResolvedEvent& evt) override;
	void                    OnRenderSamples(const net::RenderSamplesEvent& evt) override;

private:
	void                    ProcessControlInput();
	void					HandleShoot(GLFWwindow* window);
	void                    HandleGroundPick(GLFWwindow* window);
	bool                    TryGetLocalActorPosition(OUT px::Vec3& outPos) const;
	bool                    ApplyClickMoveControl();



	void                    CreateRenderingLevelData(const net::RenderLevelSpawnedEvent& evt);
	ActorRenderingData*     EnsureRenderingActorData(const net::RenderActorSpawnedEvent& evt);

	void                    BuildRenderFrames();
	void                    UpdateCamera();
	void                    RenderActors();
	void                    RenderClickMoveMarker();
	void                    RenderLevelMap();
	glm::vec3               QuatToEuler(const px::Quat& q) const;

	ActorRenderingData*     GetRenderingActorData(px::ObjectId id);
	void                    InterpolateActorTransform(const ActorRenderingData& data, float renderTick, OUT px::Vec3& pos, OUT px::Quat& rot) const;

	void                    CleanupOldSnapshots(uint32 currentTick);
	void                    GetSmoothedLocalTransform(ActorRenderingData& data, const px::Vec3& targetPos, const px::Quat& targetRot, OUT px::Vec3& outPos, OUT px::Quat& outRot);

private:
	glm::vec3										m_cameraPos		= glm::vec3(0, 10, 20);
	glm::vec3										m_cameraTarget	= glm::vec3(0, 0, 0);

	static constexpr uint32							MAX_SNAPSHOT_BUFFER = 10;
	static constexpr uint32							INTERPOLATION_DELAY = 4;

	unordered_map<px::ObjectId, ActorRenderingData> m_actorRenderData;
	uint32											m_currentRenderTick		= 0;
	uint32											m_latestServerTick		= 0;
	float											m_tickAccumulator		= 0.0f;

	bool											m_mouseInitialized		= false;
	bool											m_prevRightMouseDown	= false;
	bool											m_prevLeftMouseDown		= false;
													
	bool											m_hasMoveTarget			= false;
	bool											m_followMoveActive		= false;
	bool											m_prevMovementInputActive = false;
	px::Vec3										m_moveTarget			= px::Vec3::Zero();
	uint64											m_clickPickSeq			= 0;
	uint32											m_controlEpoch			= 0;
	float											m_clickMoveStopRadius	= 0.5f;
	float											m_clickMoveStopLeadTime = 0.10f;
	float											m_clickMoveRange		= 10000.0f;

	bool											m_hasControlSample		= false;
	px::Vec3										m_lastControlPos		= px::Vec3::Zero();
	uint64											m_lastControlNs			= 0;
	float											m_estControlSpeed		= 0.0f;
													
	float											m_topDownHeight			= 50.0f;
	float											m_topDownBackOffset		= 12.0f;
													
	float											m_lastActorY			= 0.0f;
	float											m_actorYStabilizeEps	= 0.01f;
													
	unordered_map<uint32, ActorRenderingData>		m_pendingSpawnToRenderData;
};
