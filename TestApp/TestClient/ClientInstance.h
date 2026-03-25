#pragma once

#include "Renderer.h"

#include "jamnet/runtime/ClientNetworkManager.h"
#include "jamnet/sync/replication/ReplicationEvents.h"

#include "jampx/PhysicsTypes.h"
#include "jampx/PhysicsAsset.h"



using namespace jam;

struct ActorSnapshot
{
	uint32                  tick = 0;

	optional<px::RigidState>		rigid	  = nullopt;
	optional<px::CharacterState>	character = nullopt;
};

struct ActorRenderingData
{
	glm::vec4				color{};
	px::eShapeType			shape{};
	px::Vec3				boxHalfExtents{ 0.5f, 0.5f, 0.5f };
	float                   sphereRadius = 0.5f;
	float                   capsuleRadius = 0.5f;
	float                   capsuleHalfHeight = 0.5f;
	string					meshPath;

	// NEW: 스폰 확정 전/후 상태
	bool                    ensured = false;
	uint32                  pendingSpawnReqId = 0;

	px::ObjectId			oid{};
	bool                    isLocal = false;
	deque<ActorSnapshot>    snapshots;  // 최근 N개 스냅샷 저장

	// 로컬 플레이어 렌더 스무딩 캐시
	px::Vec3				smoothedPos;
	px::Quat				smoothedRot;
	bool                    hasSmoothed = false;
	uint32                  lastSmoothedFrame = 0; // m_currentRenderTick 기준

	// 프레임 렌더 트랜스폼 캐시 (BuildRenderFrames에서 설정)
	px::Vec3				cachedRenderPos;
	px::Quat				cachedRenderRot;
	bool					renderFrameCached = false;
};

class ClientInstance
{
public:
	explicit ClientInstance(uint32 instanceId, uint64 userId);
	~ClientInstance();


	bool                                Connect(const string& serverIp, uint16 tcpPort, uint16 udpPort);
	void                                Disconnect();
	bool                                IsConnected() const;

	void                                Update(float deltaTime);
	void                                Render();

	void								SpawnActor();

	void								SpawnPlayer();		// contents
	void								SpawnPlayerDrone(); // contents
	void								SpawnBullet();


	void								DespawnActor();
	void								PossessActor();
	void								UnpossessActor();

	void								ControlCharacter(uint32 inputFlags, float pitch, float yaw);


	void                                SetWindowIndex(uint32 index) { m_windowIndex = index; }

	void                                ProcessControlInput();

	uint32                              GetInstanceId() const { return m_instanceId; }
	uint64                              GetUserId() const { return m_userId; }
	

private:

	void								OnLevelSpawned(const net::RenderLevelSpawnedEvent& evt);
	void                                OnActorSpawned(const net::RenderActorSpawnedEvent& evt);
	void                                OnActorDespawned(const net::RenderActorDespawnedEvent& evt);
	void                                OnRenderSamples(const net::RenderSamplesEvent& evt);
	
	void                                ProcessMouseLook(GLFWwindow* window);

	void								CreateRenderingLevelData(const net::RenderLevelSpawnedEvent& evt);
	ActorRenderingData*					EnsureRenderingActorData(const net::RenderActorSpawnedEvent& evt);

	void                                BuildRenderFrames();
	void                                UpdateCamera();
	void                                RenderActors();
	void								RenderLevelMap();
	glm::vec3                           QuatToEuler(const px::Quat& q) const;

	ActorRenderingData*					GetRenderingActorData(px::ObjectId id);
	void                                InterpolateActorTransform(const ActorRenderingData& data, uint32 renderTick, OUT px::Vec3& pos, OUT px::Quat& rot) const;

	void                                CleanupOldSnapshots(uint32 currentTick);

	void                                GetSmoothedLocalTransform(ActorRenderingData& data, const px::Vec3& targetPos, const px::Quat& targetRot, OUT px::Vec3& outPos, OUT px::Quat& outRot);

	
private:
	uint32										m_instanceId = 0;
	uint64										m_userId = 0;
	uint32										m_windowIndex = 0;

	unique_ptr<net::ClientNetworkManager>		m_networkManager;

	uint32										m_nextSpawnReqId = 1;

	GlobalEventBus::Subscription				m_subLevelSpawned;
	GlobalEventBus::Subscription				m_subActorSpawned;
	GlobalEventBus::Subscription				m_subActorDespawned;
	GlobalEventBus::Subscription				m_subRenderSamples;

	// 카메라 상태
	glm::vec3									m_cameraPos = glm::vec3(0, 10, 20);
	glm::vec3									m_cameraTarget = glm::vec3(0, 0, 0);


	static constexpr uint32						MAX_SNAPSHOT_BUFFER = 10;
	static constexpr uint32						INTERPOLATION_DELAY = 4;

	px::ObjectId								m_localObjectId = px::INVALID_OBJ_ID;


	unordered_map<px::ObjectId, ActorRenderingData>   m_actorRenderData;		
	uint32										m_currentRenderTick = 0;
	uint32										m_latestServerTick = 0;

	// 입력 상태

	// 마우스 룩 상태
	bool			m_mouseInitialized = false;
	bool			m_mouseCaptured = false;    // FPS 모드: ESC로 해제
	double			m_lastMouseX = 0.0;
	double			m_lastMouseY = 0.0;
	float			m_yaw = 0.0f;   // 좌우
	float			m_pitch = 0.0f; // 상하
	float			m_mouseSensitivity = 0.01f; // 감도
	float			m_mouseSmooth = 0.25f;      // 스무딩 계수

	// 카메라 기준 이동 벡터(XZ)
	glm::vec2		m_viewForwardXZ{0.0f, 1.0f};
	glm::vec2		m_viewRightXZ{1.0f, 0.0f};

	float			m_lastActorY = 0.0f;
	float			m_actorYStabilizeEps = 0.01f; // 이 이하 변동은 무시

	// =====  Level Map glTF render cache (test) =====
	unordered_map<uint32, ActorRenderingData> m_pendingSpawnToRenderData;
};